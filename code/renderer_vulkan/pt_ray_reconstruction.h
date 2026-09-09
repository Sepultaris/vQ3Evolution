/* Private RR resources; included by vk_pathtrace.c after the native PT state.
 * All Vulkan allocations are optional and unwind on failure. Native PT remains
 * valid without the SDK, on unsupported devices, and after an RR failure. */
typedef struct { VkImage image; VkImageView view; VkDeviceMemory memory; VkFormat format; } pt_rr_image_t;
#include "pt_rr_workgroup.h"
static struct {
    pt_rr_image_t images[7]; // noisy, diffuse, specular, normal/roughness, distance, HDR output, display
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline guide[2], pack, post, trace[2], spatial, fallback;
    uint32_t width, height;
    uint32_t rows, sample_index, sampling_flags;
    qboolean ready, transitioned, frame_ready, previous_active;
    qboolean direct_profile_frame;
    float push[32];
} pt_rr;

static void rr_shutdown(void)
{
    for (int i = 0; i < 2; ++i)
        if (pt_rr.guide[i]) qvkDestroyPipeline(vk.device, pt_rr.guide[i], NULL);
    if (pt_rr.pack) qvkDestroyPipeline(vk.device, pt_rr.pack, NULL);
    if (pt_rr.post) qvkDestroyPipeline(vk.device, pt_rr.post, NULL);
    for (int i = 0; i < 2; ++i)
        if (pt_rr.trace[i]) qvkDestroyPipeline(vk.device, pt_rr.trace[i], NULL);
    if (pt_rr.spatial) qvkDestroyPipeline(vk.device, pt_rr.spatial, NULL);
    if (pt_rr.fallback) qvkDestroyPipeline(vk.device, pt_rr.fallback, NULL);
    if (pt_rr.layout) qvkDestroyPipelineLayout(vk.device, pt_rr.layout, NULL);
    if (pt_rr.pool) qvkDestroyDescriptorPool(vk.device, pt_rr.pool, NULL);
    if (pt_rr.set_layout) qvkDestroyDescriptorSetLayout(vk.device, pt_rr.set_layout, NULL);
    for (int i = 0; i < 7; ++i) {
        if (pt_rr.images[i].view) qvkDestroyImageView(vk.device, pt_rr.images[i].view, NULL);
        if (pt_rr.images[i].image) qvkDestroyImage(vk.device, pt_rr.images[i].image, NULL);
        if (pt_rr.images[i].memory) qvkFreeMemory(vk.device, pt_rr.images[i].memory, NULL);
    }
    memset(&pt_rr, 0, sizeof(pt_rr));
}

static qboolean rr_pipeline(const unsigned char *code, size_t size, VkPipeline *result,
    qboolean alias_pdf)
{
    VkShaderModule module = VK_NULL_HANDLE;
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = size, .pCode = (const uint32_t *)code };
    if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) return qfalse;
    uint32_t options[4] = { VK_FALSE, alias_pdf ? VK_TRUE : VK_FALSE, VK_TRUE, pt_rr.rows };
    VkSpecializationMapEntry entries[4] = { { 0, 0, 4 }, { 1, 4, 4 }, { 2, 8, 4 }, { 3, 12, 4 } };
    VkSpecializationInfo specialization = { 4, entries, sizeof(options), options };
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pt_rr.layout, .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main",
        .pSpecializationInfo = &specialization } };
    VkResult status = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, result);
    qvkDestroyShaderModule(vk.device, module, NULL);
    return status == VK_SUCCESS;
}

qboolean vk_pt_rr_initialize(uint32_t output_width, uint32_t output_height)
{
    if (!pt.active || !vk_sl_ray_reconstruction_enabled()) return qfalse;
    extern unsigned char pt_rr_guides_comp_spv[], pt_rr_pack_comp_spv[], pt_rr_post_comp_spv[];
    extern int pt_rr_guides_comp_spv_size, pt_rr_pack_comp_spv_size, pt_rr_post_comp_spv_size;
    extern unsigned char pt_rr_trace_comp_spv[], pt_rr_spatial_comp_spv[], pt_rr_fallback_comp_spv[];
    extern int pt_rr_trace_comp_spv_size, pt_rr_spatial_comp_spv_size, pt_rr_fallback_comp_spv_size;
    VkPhysicalDeviceProperties limits;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &limits);
    if (limits.limits.maxPerStageDescriptorStorageImages < 10 ||
        limits.limits.maxPerStageDescriptorStorageBuffers < 49 ||
        output_width > limits.limits.maxImageDimension2D || output_height > limits.limits.maxImageDimension2D)
        goto fail;
    pt_rr.width = output_width; pt_rr.height = output_height;
    pt_rr.rows = rr_workgroup_rows(limits.vendorID, limits.limits.maxComputeWorkGroupSize[0],
        limits.limits.maxComputeWorkGroupSize[1], limits.limits.maxComputeWorkGroupInvocations,
        r_pathTracingRRRows->integer);
    if (!pt_rr.rows) goto fail;
    if (r_pathTracingRRRows->integer && pt_rr.rows != (uint32_t)r_pathTracingRRRows->integer)
        ri.Printf(PRINT_WARNING, "RR workgroup request %d unavailable; using 8x%u\n", r_pathTracingRRRows->integer, pt_rr.rows);
    ri.Printf(PRINT_ALL, "PT_RR_WORKGROUP rows=%u requested=%d\n", pt_rr.rows, r_pathTracingRRRows->integer);
    for (int i = 0; i < 7; ++i) {
        pt_rr_image_t *image = &pt_rr.images[i];
        image->format = i == 4 ? VK_FORMAT_R32_SFLOAT :
            (i == 6 ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT);
        VkFormatProperties properties;
        qvkGetPhysicalDeviceFormatProperties(vk.physical_device, image->format, &properties);
        const VkFormatFeatureFlags required = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | (i == 6 ? VK_FORMAT_FEATURE_BLIT_SRC_BIT : 0);
        if ((properties.optimalTilingFeatures & required) != required) goto fail;
        VkImageCreateInfo info = { .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D, .format = image->format,
            .extent = { i < 5 ? pt.width : output_width, i < 5 ? pt.height : output_height, 1 },
            .mipLevels = 1, .arrayLayers = 1, .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT };
        if (qvkCreateImage(vk.device, &info, NULL, &image->image) != VK_SUCCESS) goto fail;
        VkMemoryRequirements requirements;
        qvkGetImageMemoryRequirements(vk.device, image->image, &requirements);
        // Optional RR resources must not use the renderer's fatal allocator.
        VkPhysicalDeviceMemoryProperties memory;
        qvkGetPhysicalDeviceMemoryProperties(vk.physical_device, &memory);
        uint32_t memory_type;
        for (memory_type = 0; memory_type < memory.memoryTypeCount; ++memory_type)
            if ((requirements.memoryTypeBits & (1u << memory_type)) &&
                (memory.memoryTypes[memory_type].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) break;
        if (memory_type == memory.memoryTypeCount) goto fail;
        VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .allocationSize = requirements.size,
            .memoryTypeIndex = memory_type };
        if (qvkAllocateMemory(vk.device, &allocation, NULL, &image->memory) != VK_SUCCESS ||
            qvkBindImageMemory(vk.device, image->image, image->memory, 0) != VK_SUCCESS) goto fail;
        VkImageViewCreateInfo view = { .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image->image, .viewType = VK_IMAGE_VIEW_TYPE_2D, .format = image->format,
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        if (qvkCreateImageView(vk.device, &view, NULL, &image->view) != VK_SUCCESS) goto fail;
    }
    VkDescriptorSetLayoutBinding bindings[14] = {0};
    for (int i = 0; i < 14; ++i) bindings[i] = (VkDescriptorSetLayoutBinding){
        i, i < 7 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1, VK_SHADER_STAGE_COMPUTE_BIT, NULL };
    VkDescriptorSetLayoutCreateInfo set = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 14, .pBindings = bindings };
    if (qvkCreateDescriptorSetLayout(vk.device, &set, NULL, &pt_rr.set_layout) != VK_SUCCESS) goto fail;
    VkDescriptorPoolSize sizes[] = { { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 7 }, { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 7 } };
    VkDescriptorPoolCreateInfo pool = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 2, .pPoolSizes = sizes };
    if (qvkCreateDescriptorPool(vk.device, &pool, NULL, &pt_rr.pool) != VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo allocate = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pt_rr.pool, .descriptorSetCount = 1, .pSetLayouts = &pt_rr.set_layout };
    if (qvkAllocateDescriptorSets(vk.device, &allocate, &pt_rr.set) != VK_SUCCESS) goto fail;
    VkDescriptorImageInfo images[7];
    VkWriteDescriptorSet writes[7] = {0};
    for (int i = 0; i < 7; ++i) {
        images[i] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, pt_rr.images[i].view, VK_IMAGE_LAYOUT_GENERAL };
        writes[i] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = pt_rr.set, .dstBinding = i, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &images[i] };
    }
    qvkUpdateDescriptorSets(vk.device, 7, writes, 0, NULL);
    VkDescriptorSetLayout sets[] = { pt.set_layout, pt_rr.set_layout };
    VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 128 };
    VkPipelineLayoutCreateInfo layout = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = sets, .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
    if (qvkCreatePipelineLayout(vk.device, &layout, NULL, &pt_rr.layout) != VK_SUCCESS ||
            !rr_pipeline(pt_rr_pack_comp_spv, pt_rr_pack_comp_spv_size, &pt_rr.pack, qfalse) ||
            !rr_pipeline(pt_rr_post_comp_spv, pt_rr_post_comp_spv_size, &pt_rr.post, qfalse) ||
            !rr_pipeline(pt_rr_spatial_comp_spv, pt_rr_spatial_comp_spv_size, &pt_rr.spatial, qfalse) ||
            !rr_pipeline(pt_rr_fallback_comp_spv, pt_rr_fallback_comp_spv_size, &pt_rr.fallback, qfalse)) goto fail;
    for (int i = 0; i < 2; ++i)
        if (!rr_pipeline(pt_rr_guides_comp_spv, pt_rr_guides_comp_spv_size, &pt_rr.guide[i], i != 0) ||
            !rr_pipeline(pt_rr_trace_comp_spv, pt_rr_trace_comp_spv_size, &pt_rr.trace[i], i != 0)) goto fail;
    pt_rr.ready = qtrue;
    ri.Printf(PRINT_ALL, "Ray Reconstruction HDR resources ready: %ux%u -> %ux%u\n", pt.width, pt.height, output_width, output_height);
    return qtrue;
fail:
    rr_shutdown();
    ri.Printf(PRINT_WARNING, "Ray Reconstruction resource initialization failed; native reconstruction retained\n");
    return qfalse;
}

static qboolean rr_requested(void)
{
    return pt_rr.ready && vk_sl_ray_reconstruction_enabled() &&
        !r_pathTracingReference->integer && !r_pathTracingDebug->integer &&
        !r_pathTracingTemporalDebug->integer && !r_pathTracingShaderProfile->integer;
}

static void rr_barrier(VkCommandBuffer cmd)
{
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT };
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, NULL, 0, NULL);
}

static void rr_bind(VkCommandBuffer cmd, VkPipeline pipeline, const float *push)
{
    VkDescriptorSet sets[] = { pt.set, pt_rr.set };
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt_rr.layout, 0, 2, sets, 0, NULL);
    qvkCmdPushConstants(cmd, pt_rr.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, push);
}

static void rr_prepare(VkCommandBuffer cmd, const float *push)
{
    // Exclusive RR lifetime: repurpose already allocated native history/scratch,
    // with separate old/current descriptors. No additional per-pixel allocation.
    uint32_t i = pt_rr.sample_index ^= 1;
    const pt_buffer_t *buffers[] = {
        i ? &pt.reflection_guide : &pt.previous_reflection_guide,
        i ? &pt.previous_reflection_guide : &pt.reflection_guide,
        &pt.moments[i], &pt.moments[i ^ 1], &pt.light_change,
        &pt.history_color[i], &pt.history_color[i ^ 1] };
    VkDescriptorBufferInfo infos[7]; VkWriteDescriptorSet writes[7] = {0};
    for (int j = 0; j < 7; ++j) {
        infos[j] = (VkDescriptorBufferInfo){ buffers[j]->buffer, 0, buffers[j]->size };
        writes[j] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = pt_rr.set, .dstBinding = 7 + j, .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &infos[j] };
    }
    qvkUpdateDescriptorSets(vk.device, 7, writes, 0, NULL);
    if (!pt_rr.transitioned) {
        VkImageMemoryBarrier barriers[7] = {0};
        for (int i = 0; i < 7; ++i) barriers[i] = (VkImageMemoryBarrier){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED, .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED, .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = pt_rr.images[i].image, .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
        qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 7, barriers);
        pt_rr.transitioned = qtrue;
    }
    rr_barrier(cmd);
    rr_bind(cmd, pt_rr.guide[(pt.lighting_mode & 4) ? 1 : 0], push);
}

static void rr_pack(VkCommandBuffer cmd, const float *push, qboolean direct_output)
{
    memcpy(pt_rr.push, push, sizeof(pt_rr.push));
    rr_barrier(cmd);
    // The RR-specific tracer already wrote fresh HDR and adaptive moments.
    // Native/reference/staged transport still needs its four outputs packed.
    if (!direct_output) {
        rr_bind(cmd, pt_rr.pack, push);
        qvkCmdDispatch(cmd, (pt.width + 7) / 8, (pt.height + 7) / 8, 1);
        rr_barrier(cmd);
    }
}

qboolean vk_pt_rr_evaluate(const vk_sl_frame_resources_t *resources, VkImage *output, VkImageView *view)
{
    if (!pt_rr.frame_ready) return qfalse;
    vk_sl_frame_resources_t r = *resources;
    r.color_input = pt_rr.images[0].image; r.color_input_view = pt_rr.images[0].view;
    r.color_output = pt_rr.images[5].image; r.color_output_view = pt_rr.images[5].view;
    r.color_format = VK_FORMAT_R16G16B16A16_SFLOAT;
    r.color_input_layout = r.color_output_layout = VK_IMAGE_LAYOUT_GENERAL;
    r.exposure = r_pathTracingExposure->value;
    for (int i = 0; i < 4; ++i) {
        r.rr_guides[i] = pt_rr.images[i+1].image;
        r.rr_guide_views[i] = pt_rr.images[i+1].view;
    }
    if (!vk_sl_evaluate_ray_reconstruction(&r)) {
        // The dedicated RR tracer no longer writes an unused display image.
        // Produce it ON FAILURE before SR/native fallback consumes that image.
        rr_barrier(r.command_buffer);
        rr_bind(r.command_buffer, pt_rr.fallback, pt_rr.push);
        qvkCmdDispatch(r.command_buffer, (pt.width + 7) / 8, (pt.height + 7) / 8, 1);
        rr_barrier(r.command_buffer);
        pt.temporal_valid = qfalse;
        pt_rr.frame_ready = qfalse;
        return qfalse;
    }
    rr_barrier(r.command_buffer);
    float push[32]; memcpy(push, pt_rr.push, sizeof(push));
    push[20] = r_dlssSharpness->value;
    push[21] = r_pathTracingAdaptiveDebug->integer && (pt_rr.sampling_flags & 2u) ? 1 : 0;
    rr_bind(r.command_buffer, pt_rr.post, push);
    qvkCmdDispatch(r.command_buffer, (pt_rr.width + 7) / 8, (pt_rr.height + 7) / 8, 1);
    *output = pt_rr.images[6].image; *view = pt_rr.images[6].view;
    return qtrue;
}
