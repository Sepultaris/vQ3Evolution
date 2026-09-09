/* Execute the renderer's annotation code without a Vulkan device. */
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static struct { VkDevice device; } vk;
static unsigned begins, ends, names, depth, lookups;
static int available=1;
static VKAPI_ATTR void VKAPI_CALL begin_label(VkCommandBuffer c,const VkDebugUtilsLabelEXT *l) {
    assert(c && l->pLabelName && !depth); ++begins; ++depth;
}
static VKAPI_ATTR void VKAPI_CALL end_label(VkCommandBuffer c) { assert(c && depth==1); ++ends; --depth; }
static VKAPI_ATTR VkResult VKAPI_CALL name_object(VkDevice d,const VkDebugUtilsObjectNameInfoEXT *i) {
    (void)d; assert(i->objectType==VK_OBJECT_TYPE_PIPELINE && i->objectHandle); ++names; return VK_SUCCESS;
}
static PFN_vkVoidFunction qvkGetDeviceProcAddr(VkDevice d,const char *name) {
    (void)d; ++lookups;
    if (!available) return NULL;
    if (!strcmp(name,"vkCmdBeginDebugUtilsLabelEXT")) return (PFN_vkVoidFunction)begin_label;
    if (!strcmp(name,"vkCmdEndDebugUtilsLabelEXT")) return (PFN_vkVoidFunction)end_label;
    if (!strcmp(name,"vkSetDebugUtilsObjectNameEXT")) return (PFN_vkVoidFunction)name_object;
    return NULL;
}
#include "../code/renderer_vulkan/pt_gpu_labels.h"
int main(void) {
    VkCommandBuffer cmd=(VkCommandBuffer)(uintptr_t)1, other=(VkCommandBuffer)(uintptr_t)2;
    _putenv("VQ3E_GPU_LABELS="); gpu_labels_initialize();
    for(unsigned p=0;p<8;++p) gpu_label_point(cmd,p);
    assert(!begins && !ends && !lookups);
    _putenv("VQ3E_GPU_LABELS=1"); gpu_labels_initialize();
    for(unsigned p=0;p<8;++p) gpu_label_point(cmd,p);
    assert(begins==7 && ends==7 && !depth);
    gpu_label_point(cmd,0); gpu_label_point(other,4); gpu_label_point(cmd,6); gpu_label_point(cmd,7);
    assert(begins==9 && ends==9 && !depth);
    gpu_label_point(cmd,7); gpu_label_point(cmd,99); assert(!depth);
    gpu_label_pipeline(0,(VkPipeline)(uintptr_t)5,"test");
    gpu_label_pipeline(0,(VkPipeline)(uintptr_t)5,"test"); assert(names==1);
    gpu_label_pipeline(0,VK_NULL_HANDLE,"test"); gpu_label_pipeline(5,(VkPipeline)(uintptr_t)5,"test"); assert(names==1);
    gpu_labels_initialize(); gpu_label_pipeline(0,(VkPipeline)(uintptr_t)5,"test"); assert(names==2);
    available=0; gpu_labels_initialize();
    for(unsigned p=0;p<8;++p) gpu_label_point(cmd,p);
    assert(begins==9 && ends==9 && !depth);
    puts("PASS: disabled, balanced/skipped ranges, foreign command buffer, missing extension, pipeline names and reset");
}
