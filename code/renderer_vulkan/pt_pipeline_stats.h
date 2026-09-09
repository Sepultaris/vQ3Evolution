/* Driver compiler statistics, not invocation clock estimates. Optional and
 * read only: no buffers, GPU submissions, waits or performance-counter policy
 * changes. Capture is enabled only for explicitly requested diagnostic runs. */
static VkPipelineCreateFlags pipeline_statistics_flags(void)
{
    return r_pathTracingPipelineStats->integer && vk_rt_pipeline_statistics_supported() ?
        VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR : 0;
}

static void pipeline_statistics_print(VkPipeline pipeline, uint32_t mode, VkPipelineCreateFlags flags)
{
    if (!(flags & VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR)) return;
    PFN_vkGetPipelineExecutablePropertiesKHR properties = (PFN_vkGetPipelineExecutablePropertiesKHR)
        qvkGetDeviceProcAddr(vk.device, "vkGetPipelineExecutablePropertiesKHR");
    PFN_vkGetPipelineExecutableStatisticsKHR statistics = (PFN_vkGetPipelineExecutableStatisticsKHR)
        qvkGetDeviceProcAddr(vk.device, "vkGetPipelineExecutableStatisticsKHR");
    if (!properties || !statistics) {
        ri.Printf(PRINT_WARNING, "PT_PIPELINE_STATS_UNAVAILABLE mode=%u reason=entry_points\n", mode);
        return;
    }
    VkPipelineInfoKHR info = { .sType = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR, .pipeline = pipeline };
    uint32_t count = 0;
    if (properties(vk.device, &info, &count, NULL) != VK_SUCCESS || count == 0 || count > 256) return;
    VkPipelineExecutablePropertiesKHR *executables = ri.Malloc(count * sizeof(*executables));
    memset(executables, 0, count * sizeof(*executables));
    for (uint32_t i = 0; i < count; ++i) executables[i].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR;
    if (properties(vk.device, &info, &count, executables) == VK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i) {
            ri.Printf(PRINT_ALL, "PT_PIPELINE_EXECUTABLE mode=%u executable=%u subgroup=%u name=\"%s\"\n",
                mode, i, executables[i].subgroupSize, executables[i].name);
            VkPipelineExecutableInfoKHR executable = { .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
                .pipeline = pipeline, .executableIndex = i };
            uint32_t n = 0;
            if (statistics(vk.device, &executable, &n, NULL) != VK_SUCCESS || n == 0 || n > 256) continue;
            VkPipelineExecutableStatisticKHR *values = ri.Malloc(n * sizeof(*values));
            memset(values, 0, n * sizeof(*values));
            for (uint32_t j = 0; j < n; ++j) values[j].sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR;
            if (statistics(vk.device, &executable, &n, values) == VK_SUCCESS) {
                for (uint32_t j = 0; j < n; ++j) {
                    char value[64];
                    switch (values[j].format) {
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
                        snprintf(value, sizeof(value), "%u", values[j].value.b32); break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
                        snprintf(value, sizeof(value), "%lld", (long long)values[j].value.i64); break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
                        snprintf(value, sizeof(value), "%llu", (unsigned long long)values[j].value.u64); break;
                    case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
                        snprintf(value, sizeof(value), "%.9g", values[j].value.f64); break;
                    default: continue;
                    }
                    ri.Printf(PRINT_ALL, "PT_PIPELINE_STAT mode=%u executable=%u name=\"%s\" value=%s description=\"%s\"\n",
                        mode, i, values[j].name, value, values[j].description);
                }
            }
            ri.Free(values);
        }
    }
    ri.Free(executables);
}
