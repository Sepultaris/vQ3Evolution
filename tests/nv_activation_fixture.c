// Actual frontend submission functions; no GPU or mod state is emulated here.
#include "tr_local.h"
#include "tr_globals.h"
#include "tr_cvar.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
trGlobals_t tr;
static shader_t shaders[2];
static cvar_t enabled,override;
cvar_t *r_nvNightVision=&enabled,*r_nvOverride=&override;
static qboolean available,exhausted,restarting,nv_overlay_active;
static union { char bytes[4096]; void *alignment; } commands;
static size_t used;
qboolean vk_temporal_nv_available(void) {return available;}
qboolean vk_swapchain_restart_pending(void) {return restarting;}
void vk_sl_begin_frame(void) {}
int Q_stricmp(const char *a,const char *b) {return stricmp(a,b);}
shader_t *R_GetShaderByHandle(qhandle_t handle) {assert(handle>=0 && handle<2); return &shaders[handle];}
void *R_GetCommandBuffer(int bytes) {
    if(exhausted) return NULL;
    used=(used+7)&~(size_t)7;
    assert(used+bytes<sizeof(commands)); void *p=commands.bytes+used; used+=bytes; return p;
}
#include "nv_overlay.h"
#include "nv_submission.inc"
int main(void) {
    const char *names[]={"nvgScope2","nvgBrightA","nvgBrightB","nvgStatic","nvgScope","nvgBright",
        "gfx/items/nvgScope","gfx/items/nvgBright","gfx/items/nvgStatic","NVGSCOPE2"};
    unsigned i;
    for(i=0;i<sizeof(names)/sizeof(names[0]);++i) assert(R_IsNvOverlayName(names[i]));
    assert(!R_IsNvOverlayName("icons/items/nvg"));
    assert(!R_IsNvOverlayName("models/players/athena/nvg"));
    assert(!R_IsNvOverlayName("nvgScope2_extra"));
    tr.registered=qtrue; shaders[1].nvOverlay=qtrue;
    RE_BeginFrame(STEREO_CENTER);
    RE_StretchPic(0,0,100,100,0,0,1,1,0); // Earlier HUD command.
    assert(!R_NvOverlayActive());
    RE_StretchPic(0,0,640,480,0,0,1,1,1); // Mod submits overlay later.
    assert(R_NvOverlayActive()); // Already true before executing either command.
    RE_BeginFrame(STEREO_CENTER);
    assert(!R_NvOverlayActive()); // Toggle off / menu: no sticky previous frame.
    tr.refdef.drawSurfs=calloc(1,sizeof(drawSurf_t)); assert(tr.refdef.drawSurfs);
    surfaceType_t surface=SF_ENTITY;
    R_AddDrawSurf(&surface,&shaders[1],0,0);
    assert(R_NvOverlayActive()); // 3D brightness view also precedes backend begin_ui.
    free(tr.refdef.drawSurfs);
    RE_BeginFrame(STEREO_CENTER);
    exhausted=qtrue; RE_StretchPic(0,0,640,480,0,0,1,1,1); assert(!R_NvOverlayActive());
    exhausted=qfalse; tr.registered=qfalse;
    RE_StretchPic(0,0,640,480,0,0,1,1,1); assert(!R_NvOverlayActive());
    enabled.integer=1; available=qtrue; assert(R_NvReplaceOverlay());
    available=qfalse; assert(!R_NvReplaceOverlay()); // Keep raster fallback.
    available=qtrue; enabled.integer=0; assert(!R_NvReplaceOverlay());
    override.integer=1; assert(R_NvReplaceOverlay());
    puts("PASS: actual 2D/3D submission detects NV before backend HUD work; frame reset, unavailable/disabled fallback, exact names and dropped submissions");
    return 0;
}
