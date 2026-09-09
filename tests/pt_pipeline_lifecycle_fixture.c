/* Actual renderer functions, real Vulkan structs, mocked driver entry points.
 * This executable does not create a device or submit GPU work. */
#define VK_NO_PROTOTYPES
#include "vulkan/vulkan.h"
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
enum { qfalse, qtrue, PRINT_ALL, PRINT_WARNING };
#define ARRAY_LEN(x) (sizeof(x)/sizeof((x)[0]))
static struct { VkDevice device; } vk;
static struct {
    VkPipelineLayout layout;
    VkPipeline lighting_pipelines[64];
    uint32_t lighting_mode;
    qboolean lighting_failed[64];
} pt;
static struct { int integer; } reuse, cull, alias_pdf, emitter_geometry_cvar, shared_functions_cvar;
#define r_pathTracingBRDFReuse (&reuse)
#define r_pathTracingMapLightCull (&cull)
#define r_pathTracingAliasPDF (&alias_pdf)
#define r_pathTracingEmitterGeometry (&emitter_geometry_cvar)
#define r_pathTracingLightLoop (&shared_functions_cvar)
_Alignas(4) unsigned char pt_brdf_comp_spv[4], pathtrace_comp_spv[4];
int pt_brdf_comp_spv_size=4, pathtrace_comp_spv_size=4;
_Alignas(4) unsigned char pt_light_loop_comp_spv[4], pt_light_loop_brdf_comp_spv[4];
int pt_light_loop_comp_spv_size=4, pt_light_loop_brdf_comp_spv_size=4;
_Alignas(4) unsigned char pt_cached_materials_comp_spv[4], pt_cached_materials_brdf_comp_spv[4], pt_cached_materials_loop_comp_spv[4], pt_cached_materials_loop_brdf_comp_spv[4];
int pt_cached_materials_comp_spv_size=4, pt_cached_materials_brdf_comp_spv_size=4, pt_cached_materials_loop_comp_spv_size=4, pt_cached_materials_loop_brdf_comp_spv_size=4;
static struct { int integer; } parallel_cvar, profile_cvar;
#define r_pathTracingMaterialCache (&parallel_cvar)
#define r_pathTracingShaderProfile (&profile_cvar)
static unsigned creates[64], destroys[64], live[64], modules, module_destroys;
static uint64_t fail_mask;
static unsigned fail_next_module, partial_failure, warnings, clock_ms;
static unsigned checks;
static int compact_available;
static qboolean compact_transport_select(uint32_t mode) {
    return compact_available && !profile_cvar.integer && mode==57;
}
static void print_log(int level, const char *format, ...) {
    (void)format;
    if (level==PRINT_WARNING) ++warnings;
}
static int milliseconds(void) { clock_ms+=7; return (int)clock_ms; }
static struct { void (*Printf)(int,const char *,...); int (*Milliseconds)(void); } ri={print_log,milliseconds};
static VkPipelineCreateFlags pipeline_statistics_flags(void) { return 0; }
static void pipeline_statistics_print(VkPipeline pipeline,unsigned mode,VkPipelineCreateFlags flags) {
    assert(pipeline && mode<64 && !flags);
}
static VkResult qvkCreateShaderModule(VkDevice device, const VkShaderModuleCreateInfo *info,
    const VkAllocationCallbacks *allocator, VkShaderModule *output) {
    (void)device; (void)allocator;
    assert(info->sType==VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO && info->codeSize==4);
    if (fail_next_module) { --fail_next_module; return VK_ERROR_OUT_OF_HOST_MEMORY; }
    unsigned module_mode;
    if ((const void *)info->pCode==pathtrace_comp_spv) module_mode=0;
    else if ((const void *)info->pCode==pt_brdf_comp_spv) module_mode=1;
    else if ((const void *)info->pCode==pt_light_loop_comp_spv) module_mode=16;
    else if ((const void *)info->pCode==pt_light_loop_brdf_comp_spv) module_mode=17;
    else if ((const void *)info->pCode==pt_cached_materials_comp_spv) module_mode=32;
    else if ((const void *)info->pCode==pt_cached_materials_brdf_comp_spv) module_mode=33;
    else if ((const void *)info->pCode==pt_cached_materials_loop_comp_spv) module_mode=48;
    else { assert((const void *)info->pCode==pt_cached_materials_loop_brdf_comp_spv); module_mode=49; }
    *output=(VkShaderModule)(uintptr_t)(100+module_mode);
    ++modules;
    return VK_SUCCESS;
}
static void qvkDestroyShaderModule(VkDevice device, VkShaderModule module, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    assert((uintptr_t)module==100 || (uintptr_t)module==101 || (uintptr_t)module==116 || (uintptr_t)module==117 || (uintptr_t)module==132 || (uintptr_t)module==133 || (uintptr_t)module==148 || (uintptr_t)module==149);
    ++module_destroys;
}
static VkResult qvkCreateComputePipelines(VkDevice device, VkPipelineCache cache, uint32_t count,
    const VkComputePipelineCreateInfo *info, const VkAllocationCallbacks *allocator, VkPipeline *output) {
    (void)device; (void)cache; (void)allocator;
    assert(count==1 && info->sType==VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO && info->layout==pt.layout);
    const VkSpecializationInfo *spec=info->stage.pSpecializationInfo;
    assert(spec && spec->mapEntryCount==3 && spec->dataSize==3*sizeof(VkBool32));
    assert(spec->pMapEntries[0].constantID==0 && spec->pMapEntries[0].offset==0);
    assert(spec->pMapEntries[0].size==sizeof(VkBool32));
    assert(spec->pMapEntries[1].constantID==1 && spec->pMapEntries[1].offset==sizeof(VkBool32));
    assert(spec->pMapEntries[1].size==sizeof(VkBool32));
    assert(spec->pMapEntries[2].constantID==2 && spec->pMapEntries[2].offset==2*sizeof(VkBool32));
    assert(spec->pMapEntries[2].size==sizeof(VkBool32));
    VkBool32 values[3];
    memcpy(values,spec->pData,sizeof(values));
    for(unsigned i=0;i<3;++i) assert(values[i]==VK_TRUE || values[i]==VK_FALSE);
    unsigned mode=(unsigned)((uintptr_t)info->stage.module-100) + (values[0] ? 2u:0u) + (values[1] ? 4u:0u) + (values[2] ? 8u:0u);
    assert(mode<64 && !live[mode]);
    ++creates[mode];
    if (!(fail_mask & (UINT64_C(1)<<mode)) || partial_failure) {
        *output=(VkPipeline)(uintptr_t)(mode+1);
        live[mode]=1;
    } else *output=VK_NULL_HANDLE;
    return fail_mask & (UINT64_C(1)<<mode) ? VK_ERROR_OUT_OF_DEVICE_MEMORY:VK_SUCCESS;
}
static void qvkDestroyPipeline(VkDevice device, VkPipeline pipeline, const VkAllocationCallbacks *allocator) {
    (void)device; (void)allocator;
    unsigned mode=(unsigned)(uintptr_t)pipeline-1;
    assert(mode<64 && live[mode]);
    live[mode]=0; ++destroys[mode];
}
#include "pt_pipeline_functions.inc"
static void clear(void) {
    lighting_pipeline_shutdown();
    for(unsigned i=0;i<64;++i) assert(!live[i]);
    assert(modules==module_destroys);
    memset(&pt,0,sizeof(pt));
    memset(creates,0,sizeof(creates)); memset(destroys,0,sizeof(destroys));
    fail_mask=fail_next_module=partial_failure=warnings=modules=module_destroys=0;
    compact_available=0;
}
static int expected_mode(unsigned requested, uint64_t mask) {
    if(!(mask&(UINT64_C(1)<<requested))) return (int)requested;
    if(requested&32) return expected_mode(requested&31,mask);
    if(requested&16) return expected_mode(requested&15,mask);
    if(requested&8) return expected_mode(requested&7,mask);
    if(requested&4) return expected_mode(requested&3,mask);
    return requested==3 && !(mask&2) ? 1:!(mask&1) ? 0:-1;
}
int main(void) {
    // Every fresh configuration compiles only the requested integrator, with
    // correct shader/spec mapping. Repeated frames never compile again.
    for(unsigned mode=0;mode<64;++mode) {
        clear(); reuse.integer=(int)(mode&1); cull.integer=(int)(mode&2); alias_pdf.integer=(int)(mode&4);
        emitter_geometry_cvar.integer=(int)(mode&8);
        shared_functions_cvar.integer=(int)(mode&16);
        parallel_cvar.integer=(int)(mode&32);
        assert(lighting_pipeline_requested()==mode);
        for(unsigned frame=0;frame<1000;++frame) {
            assert(lighting_pipeline_select(lighting_pipeline_requested()));
            assert(pt.lighting_mode==mode && pt.lighting_pipelines[mode]); ++checks;
        }
        for(unsigned i=0;i<64;++i) assert(creates[i]==(i==mode));
        lighting_pipeline_shutdown(); lighting_pipeline_shutdown();
        for(unsigned i=0;i<64;++i) assert(destroys[i]==(i==mode));
    }
    // Actual same-process benchmark: proven reuse / combined candidate only;
    // the original and cull-only paths are never unnecessarily compiled.
    clear();
    const unsigned sequence[]={1,3,1,3,3,1};
    for(unsigned i=0;i<ARRAY_LEN(sequence);++i) assert(lighting_pipeline_select(sequence[i]));
    assert(creates[0]==0 && creates[1]==1 && creates[2]==0 && creates[3]==1);
    clear();
    const unsigned alias_sequence[]={1,5,1,5,5,1};
    for(unsigned i=0;i<ARRAY_LEN(alias_sequence);++i) assert(lighting_pipeline_select(alias_sequence[i]));
    for(unsigned i=0;i<16;++i) assert(creates[i]==(i==1 || i==5));
    clear();
    const unsigned geometry_sequence[]={1,9,1,9,9,1};
    for(unsigned i=0;i<ARRAY_LEN(geometry_sequence);++i) assert(lighting_pipeline_select(geometry_sequence[i]));
    for(unsigned i=0;i<16;++i) assert(creates[i]==(i==1 || i==9));
    clear();
    const unsigned shared_sequence[]={1,17,17,1,17,1};
    for(unsigned i=0;i<ARRAY_LEN(shared_sequence);++i) assert(lighting_pipeline_select(shared_sequence[i]));
    for(unsigned i=0;i<64;++i) assert(creates[i]==(i==1 || i==17));
    // All pipeline failure combinations, including non-null partial handles.
    for(unsigned mask=0;mask<65536;++mask) for(unsigned partial=0;partial<2;++partial)
        for(unsigned requested=0;requested<16;++requested) {
            clear(); fail_mask=mask; partial_failure=partial;
            int expected=expected_mode(requested,mask);
            for(unsigned frame=0;frame<8;++frame) {
                qboolean result=lighting_pipeline_select(requested);
                assert(result==(expected>=0));
                if(result) assert(pt.lighting_mode==(unsigned)expected && pt.lighting_pipelines[pt.lighting_mode]);
                for(unsigned i=0;i<16;++i) assert(creates[i]<=1);
                ++checks;
            }
        }
    // All 65,536 lower-mode failure masks, each possible shared request, and
    // both partial-handle outcomes. The shared request fails; upper unrelated
    // failures cannot affect its fallback, which strips bit 16 first.
    for(unsigned mask=0;mask<65536;++mask) for(unsigned partial=0;partial<2;++partial)
        for(unsigned requested=16;requested<32;++requested) {
            clear(); fail_mask=mask|(UINT64_C(1)<<requested); partial_failure=partial;
            int expected=expected_mode(requested,fail_mask);
            for(unsigned frame=0;frame<2;++frame) {
                qboolean result=lighting_pipeline_select(requested);
                assert(result==(expected>=0));
                if(result) assert(pt.lighting_mode==(unsigned)expected && pt.lighting_pipelines[pt.lighting_mode]);
                for(unsigned i=0;i<64;++i) assert(creates[i]<=1);
                ++checks;
            }
        }
    // Every relevant failure combination for all new upper modes. Unrelated
    // cache entries are never probed by fallback and cannot affect selection.
    for(unsigned requested=32;requested<64;++requested) {
        unsigned chain[7], count=0, node=requested;
        for(;;) {
            chain[count++]=node;
            if(!node) break;
            node=(node&32) ? node&31 : (node&16) ? node&15 : (node&8) ? node&7 :
                (node&4) ? node&3 : node==3 ? 1 : 0;
        }
        for(unsigned bits=0;bits<(1u<<count);++bits) for(unsigned partial=0;partial<2;++partial) {
            clear(); partial_failure=partial;
            for(unsigned i=0;i<count;++i) if(bits&(1u<<i)) fail_mask|=UINT64_C(1)<<chain[i];
            int expected=expected_mode(requested,fail_mask);
            for(unsigned frame=0;frame<8;++frame) {
                qboolean result=lighting_pipeline_select(requested);
                assert(result==(expected>=0));
                if(result) assert(pt.lighting_mode==(unsigned)expected);
                for(unsigned i=0;i<64;++i) assert(creates[i]<=1);
                ++checks;
            }
        }
    }
    // The cache accepts every sample count, but not shader-clock diagnostics.
    parallel_cvar.integer=1;
    for(int profile=0;profile<2;++profile) {
        profile_cvar.integer=profile;
        assert(!!(lighting_pipeline_requested()&32)==(!profile));
    }
    profile_cvar.integer=0;
    clear(); assert(lighting_pipeline_select(49));
    fail_mask=UINT64_C(1)<<17 | UINT64_C(1)<<1 | 1;
    profile_cvar.integer=1;
    assert(!lighting_pipeline_select(17)); // Never retain a wrong sample/layout kernel.
    profile_cvar.integer=0;
    assert(lighting_pipeline_select(17) && pt.lighting_mode==49);
    profile_cvar.integer=1;
    assert(!lighting_pipeline_select(17));
    profile_cvar.integer=0;
    // Shader-module failure can still use a base pipeline, and is not retried.
    clear(); fail_next_module=1;
    assert(lighting_pipeline_select(1) && pt.lighting_mode==0);
    assert(lighting_pipeline_select(1) && creates[1]==0 && creates[0]==1 && warnings==1);
    // If both a new mode and the base fail, retain the last valid pipeline.
    clear(); assert(lighting_pipeline_select(1)); fail_mask=1;
    assert(lighting_pipeline_select(0) && pt.lighting_mode==1);
    assert(!lighting_pipeline_select(64) && !lighting_pipeline_select(UINT32_MAX));
    // All variants preserve effects; a cached packed mode is a valid fallback.
    clear(); assert(lighting_pipeline_select(9)); fail_mask=1;
    assert(lighting_pipeline_select(0) && pt.lighting_mode==9);
    clear(); assert(lighting_pipeline_select(1));
    clear();
    // Default compact mode avoids compiling an unused original; toggling off
    // initializes it on demand, and failed debug switches retain a valid mode.
    compact_available=1;
    for(unsigned frame=0;frame<1000;++frame) {
        assert(lighting_pipeline_select(57) && pt.lighting_mode==57);
        for(unsigned i=0;i<64;++i) assert(creates[i]==0);
        ++checks;
    }
    fail_mask=1;
    assert(lighting_pipeline_select(0) && pt.lighting_mode==57);
    compact_available=0;
    assert(!lighting_pipeline_select(0));
    assert(lighting_pipeline_select(57) && creates[57]==1);
    clear();
    printf("PASS: %u native lifecycle checks; one default compile, on-demand base, all failure masks, clean restart, no null bind or leaked handles\n",checks);
    return 0;
}
