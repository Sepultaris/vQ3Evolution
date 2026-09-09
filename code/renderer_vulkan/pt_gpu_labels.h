/* Capture annotations only. No queries, barriers, waits or shader changes. */
static struct {
    PFN_vkCmdBeginDebugUtilsLabelEXT begin;
    PFN_vkCmdEndDebugUtilsLabelEXT end;
    PFN_vkSetDebugUtilsObjectNameEXT name;
    VkCommandBuffer command;
    VkPipeline named[5];
} pt_gpu_labels;

static void gpu_labels_initialize(void)
{
    memset(&pt_gpu_labels, 0, sizeof(pt_gpu_labels));
    const char *enabled = getenv("VQ3E_GPU_LABELS");
    if (!enabled || strcmp(enabled, "1")) return;
    /* vk_instance.c enables all advertised instance extensions. Unavailable
     * debug-utils entrypoints are a no-op, not a renderer requirement. */
    pt_gpu_labels.begin = (PFN_vkCmdBeginDebugUtilsLabelEXT)qvkGetDeviceProcAddr(vk.device, "vkCmdBeginDebugUtilsLabelEXT");
    pt_gpu_labels.end = (PFN_vkCmdEndDebugUtilsLabelEXT)qvkGetDeviceProcAddr(vk.device, "vkCmdEndDebugUtilsLabelEXT");
    pt_gpu_labels.name = (PFN_vkSetDebugUtilsObjectNameEXT)qvkGetDeviceProcAddr(vk.device, "vkSetDebugUtilsObjectNameEXT");
}

static void gpu_label_pipeline(uint32_t slot, VkPipeline pipeline, const char *name)
{
    if (!pt_gpu_labels.name || !pipeline || slot >= 5 || pt_gpu_labels.named[slot] == pipeline) return;
    VkDebugUtilsObjectNameInfoEXT info = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = VK_OBJECT_TYPE_PIPELINE, .objectHandle = (uint64_t)(uintptr_t)pipeline, .pObjectName = name };
    if (pt_gpu_labels.name(vk.device, &info) == VK_SUCCESS) pt_gpu_labels.named[slot] = pipeline;
}

static void gpu_label_point(VkCommandBuffer cmd, uint32_t point)
{
    static const char *const names[] = { "VQ3E Raster", "VQ3E Acceleration structures",
        "VQ3E Guides and uploads", "VQ3E Path tracing", "VQ3E Temporal reconstruction",
        "VQ3E Spatial reconstruction", "VQ3E NVIDIA and composition" };
    if (!pt_gpu_labels.begin || !pt_gpu_labels.end || point > 7) return;
    if (!point) {
        /* Called immediately after BeginCommandBuffer, never end an old range
         * in a reset/other command buffer. End point 7 balances this recording. */
        pt_gpu_labels.command = VK_NULL_HANDLE;
    } else if (pt_gpu_labels.command != cmd) return;
    if (pt_gpu_labels.command) pt_gpu_labels.end(cmd);
    pt_gpu_labels.command = VK_NULL_HANDLE;
    if (point == 7) return;
    VkDebugUtilsLabelEXT label = { .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = names[point], .color = {0.2f, 0.7f, 0.4f, 1.0f} };
    pt_gpu_labels.begin(cmd, &label);
    pt_gpu_labels.command = cmd;
}
