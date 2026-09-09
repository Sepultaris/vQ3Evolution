/* Optional shader-clock diagnostics. Included once after the native PT state.
 * Only the diagnostic pipeline uses set 1. Normal pipelines/buffers are intact.
 * Readback reuses the completed render fence; there is no profiling GPU wait. */
/* The first eight counters are the long-standing path/fog totals.  The second
 * eight are query-walk counters used to distinguish too many ray queries from
 * expensive traversal inside each query.  This is diagnostic-only storage;
 * production pipelines do not bind this buffer. */
typedef struct { float ticks[12]; uint32_t counts[16]; } pt_profile_record_t;
typedef char pt_profile_record_layout[(sizeof(pt_profile_record_t) == 112) ? 1 : -1];
static struct {
    VkBuffer buffer;
    VkDeviceMemory memory;
    pt_profile_record_t *mapped;
    VkDeviceSize size;
    uint32_t records;
    VkDescriptorSetLayout set_layout;
    VkDescriptorPool pool;
    VkDescriptorSet set;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    qboolean failed, pending, brdf_reuse, map_light_cull, alias_pdf, emitter_geometry, light_loop;
} pt_shader_profile;

static void shader_profile_shutdown(void)
{
    if (pt_shader_profile.pipeline) qvkDestroyPipeline(vk.device, pt_shader_profile.pipeline, NULL);
    if (pt_shader_profile.layout) qvkDestroyPipelineLayout(vk.device, pt_shader_profile.layout, NULL);
    if (pt_shader_profile.pool) qvkDestroyDescriptorPool(vk.device, pt_shader_profile.pool, NULL);
    if (pt_shader_profile.set_layout) qvkDestroyDescriptorSetLayout(vk.device, pt_shader_profile.set_layout, NULL);
    if (pt_shader_profile.mapped) qvkUnmapMemory(vk.device, pt_shader_profile.memory);
    if (pt_shader_profile.buffer) qvkDestroyBuffer(vk.device, pt_shader_profile.buffer, NULL);
    if (pt_shader_profile.memory) qvkFreeMemory(vk.device, pt_shader_profile.memory, NULL);
    memset(&pt_shader_profile, 0, sizeof(pt_shader_profile));
}

static qboolean shader_profile_initialize(qboolean brdf_reuse, qboolean map_light_cull, qboolean alias_pdf, qboolean emitter_geometry, qboolean light_loop)
{
    if (pt_shader_profile.pipeline && pt_shader_profile.brdf_reuse == brdf_reuse &&
        pt_shader_profile.map_light_cull == map_light_cull && pt_shader_profile.alias_pdf == alias_pdf &&
        pt_shader_profile.emitter_geometry == emitter_geometry && pt_shader_profile.light_loop == light_loop) return qtrue;
    // The preceding frame/readback has completed before this recording point.
    // A changed integrator needs the matching diagnostic variant, not old math.
    if (pt_shader_profile.pipeline) shader_profile_shutdown();
    if (pt_shader_profile.failed) return qfalse;
    if (!vk_rt_shader_clock_supported()) goto fail;
    VkBufferCreateInfo buffer = { .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, .sharingMode = VK_SHARING_MODE_EXCLUSIVE };
    pt_shader_profile.records = ((pt.width+63)/64)*((pt.height+63)/64)*64;
    pt_shader_profile.size = (VkDeviceSize)pt_shader_profile.records*sizeof(pt_profile_record_t);
    buffer.size = pt_shader_profile.size;
    if (qvkCreateBuffer(vk.device, &buffer, NULL, &pt_shader_profile.buffer) != VK_SUCCESS) goto fail;
    VkMemoryRequirements requirements;
    VkPhysicalDeviceMemoryProperties memory;
    qvkGetBufferMemoryRequirements(vk.device, pt_shader_profile.buffer, &requirements);
    qvkGetPhysicalDeviceMemoryProperties(vk.physical_device, &memory);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags flags = memory.memoryTypes[i].propertyFlags;
        if ((requirements.memoryTypeBits & (1u<<i)) &&
            (flags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
            (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            type = i;
            if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) break;
        }
    }
    if (type == UINT32_MAX) goto fail;
    VkMemoryAllocateInfo allocation = { .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements.size, .memoryTypeIndex = type };
    if (qvkAllocateMemory(vk.device, &allocation, NULL, &pt_shader_profile.memory) != VK_SUCCESS ||
        qvkBindBufferMemory(vk.device, pt_shader_profile.buffer, pt_shader_profile.memory, 0) != VK_SUCCESS ||
        qvkMapMemory(vk.device, pt_shader_profile.memory, 0, buffer.size, 0,
            (void **)&pt_shader_profile.mapped) != VK_SUCCESS) goto fail;
    VkDescriptorSetLayoutBinding binding = { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT };
    VkDescriptorSetLayoutCreateInfo set_info = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1, .pBindings = &binding };
    if (qvkCreateDescriptorSetLayout(vk.device, &set_info, NULL, &pt_shader_profile.set_layout) != VK_SUCCESS) goto fail;
    VkDescriptorPoolSize pool_size = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 };
    VkDescriptorPoolCreateInfo pool = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1, .poolSizeCount = 1, .pPoolSizes = &pool_size };
    if (qvkCreateDescriptorPool(vk.device, &pool, NULL, &pt_shader_profile.pool) != VK_SUCCESS) goto fail;
    VkDescriptorSetAllocateInfo sets = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = pt_shader_profile.pool, .descriptorSetCount = 1, .pSetLayouts = &pt_shader_profile.set_layout };
    if (qvkAllocateDescriptorSets(vk.device, &sets, &pt_shader_profile.set) != VK_SUCCESS) goto fail;
    VkDescriptorBufferInfo data = { pt_shader_profile.buffer, 0, buffer.size };
    VkWriteDescriptorSet write = { .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = pt_shader_profile.set, .dstBinding = 0, .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &data };
    qvkUpdateDescriptorSets(vk.device, 1, &write, 0, NULL);
    VkDescriptorSetLayout layouts[] = { pt.set_layout, pt_shader_profile.set_layout };
    VkPushConstantRange push = { VK_SHADER_STAGE_COMPUTE_BIT, 0, 128 };
    VkPipelineLayoutCreateInfo layout = { .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2, .pSetLayouts = layouts, .pushConstantRangeCount = 1, .pPushConstantRanges = &push };
    if (qvkCreatePipelineLayout(vk.device, &layout, NULL, &pt_shader_profile.layout) != VK_SUCCESS) goto fail;
    extern unsigned char pt_profile_comp_spv[];
    extern int pt_profile_comp_spv_size;
    extern unsigned char pt_profile_brdf_comp_spv[];
    extern int pt_profile_brdf_comp_spv_size;
    extern unsigned char pt_light_loop_profile_comp_spv[], pt_light_loop_profile_brdf_comp_spv[];
    extern int pt_light_loop_profile_comp_spv_size, pt_light_loop_profile_brdf_comp_spv_size;
    const unsigned char *code = light_loop ? (brdf_reuse ? pt_light_loop_profile_brdf_comp_spv : pt_light_loop_profile_comp_spv) :
        (brdf_reuse ? pt_profile_brdf_comp_spv : pt_profile_comp_spv);
    size_t code_size = light_loop ? (brdf_reuse ? pt_light_loop_profile_brdf_comp_spv_size : pt_light_loop_profile_comp_spv_size) :
        (brdf_reuse ? pt_profile_brdf_comp_spv_size : pt_profile_comp_spv_size);
    VkShaderModule module;
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = code_size, .pCode = (const uint32_t *)code };
    if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) goto fail;
    VkBool32 options[3] = { map_light_cull ? VK_TRUE : VK_FALSE, alias_pdf ? VK_TRUE : VK_FALSE, emitter_geometry ? VK_TRUE : VK_FALSE };
    VkSpecializationMapEntry entries[3] = { { 0, 0, sizeof(VkBool32) },
        { 1, sizeof(VkBool32), sizeof(VkBool32) }, { 2, 2*sizeof(VkBool32), sizeof(VkBool32) } };
    VkSpecializationInfo specialization = { 3, entries, sizeof(options), options };
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pt_shader_profile.layout,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main",
            .pSpecializationInfo = &specialization } };
    VkResult result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt_shader_profile.pipeline);
    qvkDestroyShaderModule(vk.device, module, NULL);
    if (result != VK_SUCCESS) goto fail;
    pt_shader_profile.brdf_reuse = brdf_reuse;
    pt_shader_profile.map_light_cull = map_light_cull;
    pt_shader_profile.alias_pdf = alias_pdf;
    pt_shader_profile.emitter_geometry = emitter_geometry;
    pt_shader_profile.light_loop = light_loop;
    ri.Printf(PRINT_ALL, "PT_SHADER_PROFILE_READY width=%u height=%u records=%u bytes=%u brdf_reuse=%d map_light_cull=%d alias_pdf=%d emitter_geometry=%d light_loop=%d units=relative_clock_ticks\n",
        pt.width, pt.height, pt_shader_profile.records, (uint32_t)buffer.size, brdf_reuse, map_light_cull, alias_pdf, emitter_geometry, light_loop);
    return qtrue;
fail:
    shader_profile_shutdown();
    pt_shader_profile.failed = qtrue;
    ri.Printf(PRINT_WARNING, "PT_SHADER_PROFILE_UNAVAILABLE: optional shader clock/resources or pipeline unavailable; normal rendering retained\n");
    return qfalse;
}

static void shader_profile_read(void)
{
    if (!pt_shader_profile.pending) return;
    pt_shader_profile.pending = qfalse;
    double ticks[12] = {0}, counts[16] = {0}, total = 0;
    uint32_t pixels = 0;
    for (uint32_t i = 0; i < pt_shader_profile.records; ++i) {
        const pt_profile_record_t *record = &pt_shader_profile.mapped[i];
        if (!record->counts[0]) continue; // Untouched/partial edge workgroups.
        qboolean valid = qtrue;
        for (int j = 0; j < 12; ++j)
            if (!(record->ticks[j] >= 0 && record->ticks[j] < 1e30f)) valid = qfalse;
        if (!valid) continue;
        ++pixels;
        for (int j = 0; j < 12; ++j) ticks[j] += record->ticks[j];
        for (int j = 0; j < 16; ++j) counts[j] += record->counts[j];
    }
    for (int j = 0; j < 12; ++j) total += ticks[j];
    if (total > 0 && pixels) {
        // Keep the old aggregate fields compatible. The separate fog line
        // decomposes fog math, light setup, visibility and transmittance.
        ri.Printf(PRINT_ALL, "PT_SHADER_PROFILE pixels=%u ticks=%.0f other=%.3f queries=%.3f materials=%.3f lighting=%.3f emitter=%.3f proposals=%.3f brdf=%.3f continuation=%.3f lightsetup=%.3f paths=%.0f rays=%.0f shadows=%.0f hits=%.0f\n",
            pixels, total, ticks[0]*100/total, (ticks[1]+ticks[10]+ticks[11])*100/total, (ticks[2]+ticks[8])*100/total,
            (ticks[3]+ticks[4]+ticks[5]+ticks[6]+ticks[7]+ticks[9])*100/total,
            ticks[3]*100/total, ticks[4]*100/total, ticks[5]*100/total, ticks[6]*100/total, (ticks[7]+ticks[9])*100/total,
            counts[0], counts[1], counts[2], counts[3]);
        ri.Printf(PRINT_ALL, "PT_SHADER_QUERY_PROFILE pixels=%u trace_proceed=%.0f trace_candidates=%.0f trace_confirmed=%.0f visibility_proceed=%.0f visibility_candidates=%.0f visibility_confirmed=%.0f alpha_rejected=%.0f transparent_filters=%.0f\n",
            pixels, counts[8], counts[9], counts[10], counts[11], counts[12], counts[13], counts[14], counts[15]);
        ri.Printf(PRINT_ALL, "PT_SHADER_FOG_PROFILE pixels=%u sampling=%.3f lightsetup=%.3f visibility=%.3f transmittance=%.3f events=%.0f scattered=%.0f shadows=%.0f bounded_segments=%.0f\n",
            pixels, ticks[8]*100/total, ticks[9]*100/total, ticks[10]*100/total, ticks[11]*100/total,
            counts[4], counts[5], counts[6], counts[7]);
    }
}

static qboolean shader_profile_bind(VkCommandBuffer cmd, qboolean brdf_reuse, qboolean map_light_cull, qboolean alias_pdf, qboolean emitter_geometry, qboolean light_loop)
{
    if (!r_pathTracingShaderProfile->integer || !shader_profile_initialize(brdf_reuse, map_light_cull, alias_pdf, emitter_geometry, light_loop)) return qfalse;
    /* The render fence completed before command recording. Coherent CPU writes
     * are made available to the GPU by the subsequent queue submission. */
    memset(pt_shader_profile.mapped, 0, (size_t)pt_shader_profile.size);
    VkDescriptorSet sets[] = { pt.set, pt_shader_profile.set };
    qvkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt_shader_profile.pipeline);
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt_shader_profile.layout, 0, 2, sets, 0, NULL);
    return qtrue;
}

static void shader_profile_finish(VkCommandBuffer cmd)
{
    VkMemoryBarrier barrier = { .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT, .dstAccessMask = VK_ACCESS_HOST_READ_BIT };
    qvkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    pt_shader_profile.pending = qtrue;
    // Set 0 and push constants have compatible layouts. Rebind explicitly for
    // following reconstruction passes, which do not use the diagnostic set.
    qvkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pt.layout, 0, 1, &pt.set, 0, NULL);
}
