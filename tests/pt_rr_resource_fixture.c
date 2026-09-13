/* Compile the actual optional-resource implementation against a failing driver.
 * No Vulkan loader, device, game window or GPU work is used by this fixture. */
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
enum { qfalse, qtrue, PRINT_ALL, PRINT_WARNING };
static struct { VkDevice device; VkPhysicalDevice physical_device; } vk;
typedef struct { VkBuffer buffer; VkDeviceSize size; } pt_buffer_t;
static struct { int active, temporal_valid, lighting_mode; float exposure; uint32_t width, height;
    VkDescriptorSetLayout set_layout; VkDescriptorSet set;
    pt_buffer_t reflection_guide, previous_reflection_guide, moments[2], light_change, history_color[2]; } pt;
static struct { int integer; float value; } off, exposure = { 0, 1 }, sharpness;
#define r_pathTracingReference (&off)
#define r_pathTracingRRRows (&off)
#define r_pathTracingDebug (&off)
#define r_pathTracingTemporalDebug (&off)
#define r_pathTracingShaderProfile (&off)
#define r_pathTracingExposure (&exposure)
#define r_pathTracingAdaptiveDebug (&off)
#define r_dlssSharpness (&sharpness)
static void print_log(int level, const char *format, ...) { (void)level; (void)format; }
static struct { void (*Printf)(int,const char *,...); } ri = { print_log };
_Alignas(4) unsigned char pt_rr_guides_comp_spv[4], pt_rr_pack_comp_spv[4], pt_rr_post_comp_spv[4];
int pt_rr_guides_comp_spv_size=4, pt_rr_pack_comp_spv_size=4, pt_rr_post_comp_spv_size=4;
_Alignas(4) unsigned char pt_rr_trace_comp_spv[4], pt_rr_spatial_comp_spv[4], pt_rr_fallback_comp_spv[4];
int pt_rr_trace_comp_spv_size=4, pt_rr_spatial_comp_spv_size=4, pt_rr_fallback_comp_spv_size=4;
typedef struct { VkCommandBuffer command_buffer; VkImage color_input, color_output, rr_guides[4];
    VkImageView color_input_view, color_output_view, rr_guide_views[4];
    VkFormat color_format; VkImageLayout color_input_layout, color_output_layout;
    float exposure; } vk_sl_frame_resources_t;
static int enabled=1, evaluate_ok=1, fail_at, operations, next_handle, live_count, no_memory, no_format, low_limits;
static unsigned char live[256];
static VkPipeline bound_pipeline;
static int fallback_dispatches, pack_dispatches, barrier_count;
static VkBuffer sampling_buffers[7];
static qboolean vk_sl_ray_reconstruction_enabled(void) { return enabled; }
static qboolean vk_sl_evaluate_ray_reconstruction(const vk_sl_frame_resources_t *r) {
    assert(r->color_format==VK_FORMAT_R16G16B16A16_SFLOAT);
    assert(r->exposure==pt.exposure);
    for(int i=0;i<4;++i) assert(r->rr_guides[i] && r->rr_guide_views[i]);
    return evaluate_ok;
}
static VkResult operation(void) { return ++operations==fail_at ? VK_ERROR_OUT_OF_DEVICE_MEMORY : VK_SUCCESS; }
static VkResult create_handle(void *output, size_t size) {
    assert(size==sizeof(uintptr_t));
    memset(output,0,size);
    if(operation()!=VK_SUCCESS) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    uintptr_t id=++next_handle; assert(id<sizeof(live));
    live[id]=1; ++live_count; memcpy(output,&id,size); return VK_SUCCESS;
}
static void destroy_handle(uintptr_t id) { assert(id && id<sizeof(live) && live[id]); live[id]=0; --live_count; }
#define CREATE(out) create_handle((out),sizeof(*(out)))
#define qvkCreateImage(d,i,a,o) CREATE(o)
#define qvkAllocateMemory(d,i,a,o) CREATE(o)
#define qvkCreateImageView(d,i,a,o) CREATE(o)
#define qvkCreateDescriptorSetLayout(d,i,a,o) CREATE(o)
#define qvkCreateDescriptorPool(d,i,a,o) CREATE(o)
#define qvkCreatePipelineLayout(d,i,a,o) CREATE(o)
#define qvkCreateShaderModule(d,i,a,o) CREATE(o)
#define qvkCreateComputePipelines(d,c,n,i,a,o) CREATE(o)
#define qvkDestroyPipeline(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkDestroyPipelineLayout(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkDestroyDescriptorPool(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkDestroyDescriptorSetLayout(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkDestroyImageView(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkDestroyImage(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkFreeMemory(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkDestroyShaderModule(d,h,a) destroy_handle((uintptr_t)(h))
#define qvkBindImageMemory(d,i,m,o) operation()
#define qvkAllocateDescriptorSets(d,i,o) (*(o)=(VkDescriptorSet)(uintptr_t)255,operation())
static void update_descriptors(uint32_t count,const VkWriteDescriptorSet *writes) {
    for(uint32_t i=0;i<count;++i) if(writes[i].descriptorType==VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
        assert(writes[i].dstBinding>=7 && writes[i].dstBinding<14);
        sampling_buffers[writes[i].dstBinding-7]=writes[i].pBufferInfo->buffer;
        assert(writes[i].pBufferInfo->range>0);
    }
}
#define qvkUpdateDescriptorSets(d,n,w,c,p) update_descriptors(n,w)
#define qvkCmdPipelineBarrier(...) (++barrier_count)
#define qvkCmdBindPipeline(c,b,p) (bound_pipeline=(p))
#define qvkCmdBindDescriptorSets(...) ((void)0)
#define qvkCmdPushConstants(...) ((void)0)
static void record_dispatch(void);
#define qvkCmdDispatch(...) record_dispatch()
static void qvkGetPhysicalDeviceProperties(VkPhysicalDevice d,VkPhysicalDeviceProperties *p) {
    (void)d; memset(p,0,sizeof(*p)); p->limits.maxPerStageDescriptorStorageImages=low_limits==1?9:64;
    p->limits.maxImageDimension2D=8192;
    p->limits.maxComputeWorkGroupSize[0]=8;
    p->limits.maxComputeWorkGroupSize[1]=128;
    p->limits.maxComputeWorkGroupInvocations=1024;
    p->limits.maxPerStageDescriptorStorageBuffers=low_limits==2?48:64;
}
static void qvkGetPhysicalDeviceFormatProperties(VkPhysicalDevice d,VkFormat f,VkFormatProperties *p) {
    (void)d; (void)f; memset(p,0,sizeof(*p)); p->optimalTilingFeatures=no_format?0:~0u;
}
static void qvkGetImageMemoryRequirements(VkDevice d,VkImage i,VkMemoryRequirements *r) {
    (void)d; (void)i; memset(r,0,sizeof(*r)); r->size=1024; r->memoryTypeBits=1;
}
static void qvkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice d,VkPhysicalDeviceMemoryProperties *p) {
    (void)d; memset(p,0,sizeof(*p)); p->memoryTypeCount=1;
    p->memoryTypes[0].propertyFlags=no_memory?0:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}
#include "../code/renderer_vulkan/pt_ray_reconstruction.h"
static void record_dispatch(void) {
    if(bound_pipeline==pt_rr.fallback) ++fallback_dispatches;
    if(bound_pipeline==pt_rr.pack) ++pack_dispatches;
}
static void reset(void) {
    assert(!live_count); operations=next_handle=0; pt.active=1; pt.width=1920; pt.height=1080;
    pt.lighting_mode=0; pt.exposure=1.75f;
    pt_buffer_t *buffers[]={&pt.reflection_guide,&pt.previous_reflection_guide,&pt.moments[0],
        &pt.moments[1],&pt.light_change,&pt.history_color[0],&pt.history_color[1]};
    for(uintptr_t i=0;i<7;++i) *buffers[i]=(pt_buffer_t){(VkBuffer)(i+100),4096};
}
int main(void) {
    reset(); assert(vk_pt_rr_initialize(1920,1080));
    int total=operations; assert(total>30 && pt_rr.ready && rr_requested());
    off.integer=1; assert(!rr_requested()); off.integer=0;
    float push[32]={0}; rr_prepare(VK_NULL_HANDLE,push);
    assert(bound_pipeline==pt_rr.guide[0]);
    pt.lighting_mode=4; rr_prepare(VK_NULL_HANDLE,push);
    assert(bound_pipeline==pt_rr.guide[1]);
    pt.lighting_mode=0;
    rr_pack(VK_NULL_HANDLE,push,qfalse);
    assert(pack_dispatches==1);
    int old_barriers=barrier_count;
    push[23]=123.0f;
    rr_pack(VK_NULL_HANDLE,push,qtrue);
    assert(pack_dispatches==1 && barrier_count>old_barriers && pt_rr.push[23]==123.0f);
    VkBuffer last[7]; memcpy(last,sampling_buffers,sizeof(last));
    for(int a=0;a<7;++a) for(int b=a+1;b<7;++b) assert(last[a]!=last[b]);
    rr_prepare(VK_NULL_HANDLE,push);
    assert(last[0]==sampling_buffers[1] && last[1]==sampling_buffers[0]);
    assert(last[2]==sampling_buffers[3] && last[3]==sampling_buffers[2]);
    assert(last[5]==sampling_buffers[6] && last[6]==sampling_buffers[5]);
    assert(last[4]==sampling_buffers[4]);
    pt_rr.frame_ready=1; pt.temporal_valid=1;
    vk_sl_frame_resources_t resources={0}; VkImage output=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE;
    assert(vk_pt_rr_evaluate(&resources,&output,&view) && output && view);
    assert(fallback_dispatches==0);
    evaluate_ok=0; assert(!vk_pt_rr_evaluate(&resources,&output,&view));
    assert(!pt_rr.frame_ready && !pt.temporal_valid);
    assert(fallback_dispatches==1); // A failed RR frame MUST populate the SR input.
    rr_shutdown(); rr_shutdown(); assert(!live_count);
    for(fail_at=1;fail_at<=total;++fail_at) {
        reset(); assert(!vk_pt_rr_initialize(1920,1080));
        assert(!pt_rr.ready && !live_count && pt.active);
        rr_shutdown(); assert(!live_count);
    }
    fail_at=0;
    reset(); no_memory=1; assert(!vk_pt_rr_initialize(1920,1080) && !live_count); no_memory=0;
    reset(); no_format=1; assert(!vk_pt_rr_initialize(1920,1080) && !live_count); no_format=0;
    reset(); low_limits=1; assert(!vk_pt_rr_initialize(1920,1080) && !live_count); low_limits=0;
    reset(); low_limits=2; assert(!vk_pt_rr_initialize(1920,1080) && !live_count); low_limits=0;
    reset(); enabled=0; assert(!vk_pt_rr_initialize(1920,1080) && !operations);
    printf("PASS: %d creation/binding failure points, capability failures, evaluation failure, and double shutdown\n",total);
    return 0;
}
