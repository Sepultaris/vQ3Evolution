// Compact state has the exact feature set of lighting mode 57.
// Other diagnostic combinations retain their existing integrator unchanged.
static qboolean compact_transport_select(uint32_t mode)
{
    if (!r_pathTracingCompactTransport->integer || r_pathTracingShaderProfile->integer || mode != 57)
        return qfalse;
    if (pt.compact_transport_pipeline) return qtrue;
    if (pt.compact_transport_failed) return qfalse;
    // Larger groups constrain the NVIDIA compiler's per-thread
    // register budget without outlining material calls or reducing ray work.
    // Keep the portable 8x8 variant on other vendors and as a creation fallback.
    VkPhysicalDeviceProperties properties;
    qvkGetPhysicalDeviceProperties(vk.physical_device, &properties);
    pt.compact_transport_rows = 8;
    if (properties.vendorID == 0x10de && properties.limits.maxComputeWorkGroupSize[0] >= 8) {
        if (properties.limits.maxComputeWorkGroupInvocations >= 1024 && properties.limits.maxComputeWorkGroupSize[1] >= 128)
            pt.compact_transport_rows = 128;
        else if (properties.limits.maxComputeWorkGroupInvocations >= 512 && properties.limits.maxComputeWorkGroupSize[1] >= 64)
            pt.compact_transport_rows = 64;
    }
    extern unsigned char pt_compact_transport_comp_spv[];
    extern int pt_compact_transport_comp_spv_size;
    VkShaderModule module;
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = pt_compact_transport_comp_spv_size, .pCode = (const uint32_t *)pt_compact_transport_comp_spv };
    if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) goto fail;
    uint32_t options[4] = { VK_FALSE, VK_FALSE, VK_TRUE, pt.compact_transport_rows };
    VkSpecializationMapEntry entries[4] = { { 0, 0, sizeof(uint32_t) },
        { 1, 4, sizeof(uint32_t) }, { 2, 8, sizeof(uint32_t) }, { 3, 12, sizeof(uint32_t) } };
    VkSpecializationInfo specialization = { 4, entries, sizeof(options), options };
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .flags = pipeline_statistics_flags(), .layout = pt.layout,
        .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main",
            .pSpecializationInfo = &specialization } };
    for (;;) {
        ri.Printf(PRINT_ALL, "PT_COMPACT_WORKGROUP rows=%u\n", pt.compact_transport_rows);
        ri.Printf(PRINT_ALL, "PT_COMPACT_PIPELINE_BEGIN\n");
        int started = ri.Milliseconds();
        VkResult result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.compact_transport_pipeline);
        ri.Printf(PRINT_ALL, "PT_COMPACT_PIPELINE_END cpu_ms=%d result=%d\n", ri.Milliseconds() - started, (int)result);
        if (result == VK_SUCCESS) {
            qvkDestroyShaderModule(vk.device, module, NULL);
            pipeline_statistics_print(pt.compact_transport_pipeline, 121, pipeline.flags);
            return qtrue;
        }
        if (pt.compact_transport_pipeline) qvkDestroyPipeline(vk.device, pt.compact_transport_pipeline, NULL);
        pt.compact_transport_pipeline = VK_NULL_HANDLE;
        if (pt.compact_transport_rows == 8) break;
        options[3] = pt.compact_transport_rows = pt.compact_transport_rows == 128 ? 64 : 8;
        ri.Printf(PRINT_WARNING, "PT_COMPACT_WORKGROUP_FALLBACK: retrying 8x%u\n", pt.compact_transport_rows);
    }
    qvkDestroyShaderModule(vk.device, module, NULL);
fail:
    pt.compact_transport_failed = qtrue;
    ri.Printf(PRINT_WARNING, "PT_COMPACT_UNAVAILABLE: retaining original transport\n");
    return qfalse;
}
