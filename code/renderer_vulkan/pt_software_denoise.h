/* Mode-1-only optional denoiser loading. This file contains no SDK code. */
#include "nrd/software_denoiser.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
static struct {
    void *module, *instance;
    vqe_denoiser_create_t create;
    vqe_denoiser_record_t record;
    vqe_denoiser_destroy_t destroy;
    VkImageView output;
    unsigned int last_frame;
    qboolean failed, logged;
} sw_denoise;
static cvar_t *sw_history, *sw_filter;

static void sw_denoise_shutdown(void)
{
    if(sw_denoise.instance) sw_denoise.destroy(sw_denoise.instance);
#ifdef _WIN32
    if(sw_denoise.module) FreeLibrary((HMODULE)sw_denoise.module);
#endif
    memset(&sw_denoise,0,sizeof(sw_denoise));
}

static void sw_denoise_initialize(VkImageView output)
{
    sw_history=ri.Cvar_Get("r_softwareRayTracingHistory", "1", CVAR_ARCHIVE);
    sw_filter=ri.Cvar_Get("r_softwareRayTracingDenoiser", "0", CVAR_ARCHIVE | CVAR_LATCH);
    ri.Cvar_CheckRange(sw_history,0,1,qtrue);
    ri.Cvar_CheckRange(sw_filter,0,1,qtrue);
    if(!pt.software || !sw_filter->integer) return;
    sw_denoise.output=output;
#ifdef _WIN32
    wchar_t path[32768];
    DWORD count=GetModuleFileNameW(NULL,path,ARRAY_LEN(path));
    if(count && count<ARRAY_LEN(path)) {
        wchar_t *slash=wcsrchr(path,L'\\');
        if(slash && (size_t)(slash-path)+20<ARRAY_LEN(path)) {
            wcscpy(slash+1,L"vq3e_nrd.dll");
            sw_denoise.module=LoadLibraryExW(path,NULL,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
        }
    }
    if(sw_denoise.module) {
        sw_denoise.create=(vqe_denoiser_create_t)GetProcAddress(sw_denoise.module,"vqe_denoiser_create");
        sw_denoise.record=(vqe_denoiser_record_t)GetProcAddress(sw_denoise.module,"vqe_denoiser_record");
        sw_denoise.destroy=(vqe_denoiser_destroy_t)GetProcAddress(sw_denoise.module,"vqe_denoiser_destroy");
    }
#endif
    char error[512]="Optional vq3e_nrd.dll missing or incompatible";
    if(sw_denoise.create && sw_denoise.record && sw_denoise.destroy) {
        vqe_denoiser_init_t init={VQE_DENOISER_ABI,pt.width,pt.height,vk.instance,
            vk.physical_device,vk.device,qvkGetInstanceProcAddr,qvkGetDeviceProcAddr};
        sw_denoise.instance=sw_denoise.create(&init,error,sizeof(error));
    }
    if(!sw_denoise.instance) {
        ri.Printf(PRINT_WARNING,"Software NRD unavailable: %s; using native denoising\n",error);
        sw_denoise_shutdown();
    } else ri.Printf(PRINT_ALL,"Software NRD RELAX ready (%ux%u), no RTX features required\n",pt.width,pt.height);
}

static qboolean sw_denoise_record(VkCommandBuffer command,const float *camera)
{
    if(!pt.software || !sw_denoise.instance || sw_denoise.failed || r_pathTracingDebug->integer ||
        r_pathTracingTemporalDebug->integer || !r_pathTracingTemporal->integer) return qfalse;
    vqe_denoiser_frame_t frame={0};
    frame.command=command; frame.output=sw_denoise.output;
    const VkBuffer buffers[VQE_DENOISE_BUFFERS]={pt.accumulation.buffer,pt.specular.buffer,
        pt.guides[0].buffer,pt.guides[2].buffer,pt.shading_guide.buffer,pt.motion_guides[0].buffer,
        pt.transmission_history[pt.reconstruction_index].buffer,pt.visible_emission.buffer,
        pt.software_metadata.buffer,pt.light_change.buffer,
        pt.history_color[pt.reconstruction_index].buffer,pt.spec_history[pt.reconstruction_index].buffer};
    memcpy(frame.buffers,buffers,sizeof(buffers));
    memcpy(frame.camera,camera,sizeof(frame.camera));
    memcpy(frame.previous,pt.temporal_params.mapped,sizeof(frame.previous));
    frame.frame=pt.frame; frame.history=r_pathTracingHistory->integer;
    /* Debug views and temporal-off frames bypass NRD; never reuse that stale history. */
    frame.reset=frame.previous[19]!=0 || !sw_denoise.logged || frame.frame!=sw_denoise.last_frame+1u;
    char error[512]={0};
    if(!sw_denoise.record(sw_denoise.instance,&frame,error,sizeof(error))) {
        sw_denoise.failed=qtrue;
        ri.Printf(PRINT_WARNING,"Software NRD failed: %s; using native denoising\n",error);
        return qfalse;
    }
    if(!sw_denoise.logged) ri.Printf(PRINT_ALL,"Software NRD RELAX active: diffuse + specular; native optical/effect fallback\n");
    sw_denoise.logged=qtrue;
    sw_denoise.last_frame=frame.frame;
    return qtrue;
}
