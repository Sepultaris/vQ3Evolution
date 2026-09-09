// Optional per-frame material preprocessing; no new buffer or descriptor.
static qboolean material_cache_initialize(void)
{
    if (pt.material_cache_pipeline) return qtrue;
    if (pt.material_cache_failed) return qfalse;
    extern unsigned char pt_material_cache_comp_spv[];
    extern int pt_material_cache_comp_spv_size;
    VkShaderModule module;
    VkShaderModuleCreateInfo shader = { .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = pt_material_cache_comp_spv_size, .pCode = (const uint32_t *)pt_material_cache_comp_spv };
    if (qvkCreateShaderModule(vk.device, &shader, NULL, &module) != VK_SUCCESS) goto fail;
    VkComputePipelineCreateInfo pipeline = { .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .layout = pt.layout, .stage = { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT, .module = module, .pName = "main" } };
    VkResult result = qvkCreateComputePipelines(vk.device, VK_NULL_HANDLE, 1, &pipeline, NULL, &pt.material_cache_pipeline);
    qvkDestroyShaderModule(vk.device, module, NULL);
    if (result == VK_SUCCESS) return qtrue;
    if (pt.material_cache_pipeline) qvkDestroyPipeline(vk.device, pt.material_cache_pipeline, NULL);
    pt.material_cache_pipeline = VK_NULL_HANDLE;
fail:
    pt.material_cache_failed = qtrue;
    ri.Printf(PRINT_WARNING, "PT_MATERIAL_CACHE_UNAVAILABLE: keeping original material evaluation\n");
    return qfalse;
}
