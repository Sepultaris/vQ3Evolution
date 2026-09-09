// GPU-free execution of the production compact shader lifecycle.
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"
#include <assert.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
enum { qfalse, qtrue, PRINT_ALL, PRINT_WARNING };
static struct { VkDevice device; VkPhysicalDevice physical_device; } vk;
static VkPhysicalDeviceProperties device_properties;
static void qvkGetPhysicalDeviceProperties(VkPhysicalDevice device,VkPhysicalDeviceProperties *out) {
    (void)device;*out=device_properties;
}
static struct {
    VkPipelineLayout layout;
    VkPipeline compact_transport_pipeline;
    uint32_t compact_transport_rows;
    qboolean compact_transport_failed;
} pt;
static struct { int integer; } requested,profile;
#define r_pathTracingCompactTransport (&requested)
#define r_pathTracingShaderProfile (&profile)
_Alignas(4) unsigned char pt_compact_transport_comp_spv[4];
int pt_compact_transport_comp_spv_size=4;
static unsigned failure,modules,module_destroys,creates,destroys,live,warnings;
static void print_log(int level,const char *fmt,...) { (void)fmt;if(level==PRINT_WARNING) ++warnings; }
static int milliseconds(void) { return 0; }
static struct {void (*Printf)(int,const char *,...);int (*Milliseconds)(void);}ri={print_log,milliseconds};
static VkPipelineCreateFlags pipeline_statistics_flags(void) { return 0; }
static void pipeline_statistics_print(VkPipeline p,unsigned mode,VkPipelineCreateFlags flags) {
    assert(p && mode==121 && !flags);
}
static VkResult qvkCreateShaderModule(VkDevice d,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *a,VkShaderModule *out) {
    (void)d;(void)a;assert(info->codeSize==4 && (const void *)info->pCode==pt_compact_transport_comp_spv);
    ++modules;if(failure==1) return VK_ERROR_OUT_OF_HOST_MEMORY;
    *out=(VkShaderModule)(uintptr_t)1;return VK_SUCCESS;
}
static void qvkDestroyShaderModule(VkDevice d,VkShaderModule m,const VkAllocationCallbacks *a) {
    (void)d;(void)a;assert(m);++module_destroys;
}
static VkResult qvkCreateComputePipelines(VkDevice d,VkPipelineCache c,uint32_t count,const VkComputePipelineCreateInfo *info,const VkAllocationCallbacks *a,VkPipeline *out) {
    (void)d;(void)c;(void)a;assert(count==1 && info->layout==pt.layout && !live);
    const VkSpecializationInfo *s=info->stage.pSpecializationInfo;
    assert(s && s->mapEntryCount==4 && s->dataSize==4*sizeof(uint32_t));
    const VkBool32 *v=s->pData;
    for(unsigned i=0;i<3;++i) {
        assert(s->pMapEntries[i].constantID==i && s->pMapEntries[i].offset==i*sizeof(VkBool32));
        assert(s->pMapEntries[i].size==sizeof(VkBool32) && v[i]==(i==2 ? VK_TRUE:VK_FALSE));
    }
    assert(s->pMapEntries[3].constantID==3 && s->pMapEntries[3].offset==12 && s->pMapEntries[3].size==4);
    assert(v[3]==pt.compact_transport_rows && (v[3]==8 || v[3]==64 || v[3]==128));
    ++creates;if(failure!=2) { *out=(VkPipeline)(uintptr_t)1;live=1; }
    return failure==2 || failure==3 || (failure==4 && v[3]>8) || (failure==5 && v[3]==128) ? VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static void qvkDestroyPipeline(VkDevice d,VkPipeline p,const VkAllocationCallbacks *a) {
    (void)d;(void)a;assert(p && live);live=0;++destroys;
}
#include "../code/renderer_vulkan/pt_compact_transport.h"
int main(void) {
    unsigned checks=0;
    for(unsigned mode=0;mode<64;++mode) for(unsigned on=0;on<2;++on)
    for(unsigned prof=0;prof<2;++prof) for(unsigned fail=0;fail<6;++fail) for(unsigned device=0;device<6;++device) {
        memset(&pt,0,sizeof(pt));modules=module_destroys=creates=destroys=live=warnings=0;
        memset(&device_properties,0,sizeof(device_properties));
        device_properties.vendorID=device ? 0x10de:0x1002;
        device_properties.limits.maxComputeWorkGroupInvocations=device==2 ? 256:(device==4 ? 512:1024);
        device_properties.limits.maxComputeWorkGroupSize[0]=1024;
        device_properties.limits.maxComputeWorkGroupSize[1]=device==3 ? 32:(device==5 ? 64:1024);
        unsigned rows=device==1 ? 128:(device>=4 ? 64:8);
        failure=fail;requested.integer=on;profile.integer=prof;
        unsigned eligible=on && !prof && mode==57;
        unsigned success=fail==0 || fail>=4;
        unsigned retry=fail>=2 && fail<=4 ? (rows==128 ? 2:(rows==64 ? 1:0)) : (fail==5 && rows==128);
        for(unsigned frame=0;frame<8;++frame) {
            assert(compact_transport_select(mode)==(int)(eligible && success));
            assert(modules==eligible && creates==eligible*(fail!=1)*(1+retry));
            assert(live==(eligible && success));
            assert(warnings==eligible*((!success)+retry));
            if(eligible && success) assert(pt.compact_transport_rows==(fail==4 ? 8u:(fail==5 && rows==128 ? 64u:rows)));
            ++checks;
        }
        if(pt.compact_transport_pipeline) qvkDestroyPipeline(vk.device,pt.compact_transport_pipeline,NULL);
        assert(!live && module_destroys==(eligible && fail!=1));
        assert(destroys==eligible*(fail!=1 && fail!=2)*(1+retry));
    }
    printf("PASS: %u production compact-pipeline checks; incompatible modes, partial failures, no repeated compilation or leaks\n",checks);
    return 0;
}
