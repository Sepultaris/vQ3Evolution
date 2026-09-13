"""Production menu callbacks, FG request/cap policy, and optional live counters.

No game launch or user settings changes. SDK checks use the locally fetched SDK.
"""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile

from pt_frame_generation_check import check as check_presentations, STATS

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    start = source.index(name + "(")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


def offline(compiler):
    ui = (ROOT / 'code/q3_ui/ui_video.c').read_text()
    sl = (ROOT / 'code/renderer_vulkan/vk_streamline.cpp').read_text()
    cvars = (ROOT / 'code/renderer_vulkan/tr_cvar.c').read_text()
    assert 'raytracing_names[] = { "Raster", "Path tracing", NULL }' in ui
    assert 's_graphicsoptions.raytracing.curvalue = s_graphicsoptions.raytracingMode == 2 ? 1 : 0;' in ui
    apply = function(ui, 'GraphicsOptions_ApplyChanges')
    assert '"r_rayTracing", s_graphicsoptions.raytracingMode' in apply
    assert '"r_dlssNeuralRendering"' not in apply and '"r_dlssNR' not in apply
    assert 's_ivo.fgmultiplier != s_graphicsoptions.fgmultiplier.curvalue' in apply
    assert 's_graphicsoptions.raytracingMode != 2 ||' in ui
    assert 'Cvar_Set( "r_dlssFrameGenerationMaxMultiplier", "0" )' not in cvars
    assert 'r_rayTracing, "0 raster, 1 software tracing (WIP; console-only)' in cvars
    assert 'r_dlssNeuralRendering, "WIP, console-only' in cvars
    assert sl.index('state.numFramesToGenerateMax > 0') < sl.index('unloadIdleFrameGenerationHooks();')
    active = function(sl, 'vk_sl_set_frame_generation_active')
    assert not re.search(r'numFramesToGenerate\s*=', active)  # Focus changes retain configured multiplier.
    assert 'GetForegroundWindow()' in function(sl, 'gameWindowHasForegroundFocus')
    assert 'frameGenerationUnfocusedQueries += !gameWindowHasForegroundFocus()' in sl

    # Compile the actual production callbacks and FG configuration against the
    # real SDK structures; mocks only supply device state and record API inputs.
    capability = sl[sl.index('\tif (g_sl.dlssGGetState && g_sl.dlssGSetOptions) {'):sl.index('\tfunction = nullptr;', sl.index('state.numFramesToGenerateMax > 0'))]
    request = sl[sl.index('\tframeGeneration = frameGeneration &&'):sl.index('\tg_sl.frameGenerationActive = false;', sl.index('\tframeGeneration = frameGeneration &&'))]
    options = sl[sl.index('\tif (g_sl.dlssGSetOptions) {', sl.index('qboolean vk_sl_configure')):sl.index('\n\tri.Printf(PRINT_ALL, "NVIDIA DLSS mode')]
    fixture = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <sl_dlss_g.h>
enum { PRINT_WARNING, QM_ACTIVATED=3 };
static int requested, limit;
static bool queryOK, setOK;
static unsigned generated;
static sl::Result getState(const sl::ViewportHandle&, sl::DLSSGState &s, const sl::DLSSGOptions*) {
    s.numFramesToGenerateMax=limit;
    return queryOK ? sl::Result::eOk : sl::Result::eErrorInvalidParameter;
}
static sl::Result setOptions(const sl::ViewportHandle&, const sl::DLSSGOptions &s) {
    generated=s.numFramesToGenerate;
    assert(s.mode==sl::DLSSGMode::eOff && s.colorWidth==1920 && s.mvecDepthWidth==1920);
    return setOK ? sl::Result::eOk : sl::Result::eErrorInvalidParameter;
}
static int cvar(const char*) { return requested; }
static void setCvar(const char*,const char*) {}
static void print(int,const char*,...) {}
static struct { int(*Cvar_VariableIntegerValue)(const char*); void(*Cvar_Set)(const char*,const char*); void(*Printf)(int,const char*,...); } ri={cvar,setCvar,print};
static struct {
    bool frameGenerationSupported,frameGenerationPresentationSupported,frameGenerationEnabled,frameGenerationStateLogged;
    unsigned frameGenerationRequestedMultiplier,frameGenerationMultiplier,frameGenerationMaxMultiplier;
    PFun_slDLSSGGetState *dlssGGetState;
    PFun_slDLSSGSetOptions *dlssGSetOptions;
    sl::DLSSGOptions dlssGOptions;
} g_sl;
static float Com_Clamp(float a,float b,float v) { return std::max(a,std::min(b,v)); }
static float trap_Cvar_VariableValue(const char*) { return (float)limit; }
struct menuslider_s { float curvalue; };
static struct { struct { int curvalue; } raytracing; int raytracingMode; } s_graphicsoptions;
''' + 'static void ' + function(ui, 'GraphicsOptions_RayTracingEvent') + '\nstatic void ' + function(ui, 'GraphicsOptions_FGMultiplierEvent') + r'''
static void capability() {
''' + capability + r'''
}
static void configure(int frameGeneration) {
    unsigned outputWidth=1920,outputHeight=1080,w=1920,h=1080,*renderWidth=&w,*renderHeight=&h;
''' + request + options + r'''
}
int main() {
    for(int cap=1;cap<=5;++cap) for(int want=0;want<=8;++want) {
        requested=want; limit=cap; queryOK=setOK=true;
        g_sl={}; g_sl.frameGenerationSupported=true; g_sl.frameGenerationPresentationSupported=true;
        g_sl.dlssGGetState=getState; g_sl.dlssGSetOptions=setOptions;
        capability(); assert(g_sl.frameGenerationMaxMultiplier==(unsigned)(cap+1));
        configure(1);
        assert(g_sl.frameGenerationEnabled);
        assert(generated==(unsigned)(std::min(std::max(2,std::min(6,want)),cap+1)-1));
        assert(g_sl.frameGenerationRequestedMultiplier==(unsigned)std::max(2,std::min(6,want)));
        setOK=false; configure(1); assert(!g_sl.frameGenerationEnabled);
        setOK=true; configure(0); assert(!g_sl.frameGenerationEnabled);
        g_sl.frameGenerationPresentationSupported=false; configure(1); assert(!g_sl.frameGenerationEnabled);
    }
    for(int failure=0;failure<4;++failure) {
        g_sl={}; g_sl.frameGenerationSupported=true;
        g_sl.dlssGGetState=failure==0 ? nullptr:getState;
        g_sl.dlssGSetOptions=failure==1 ? nullptr:setOptions;
        queryOK=failure!=2; limit=failure==3 ? 0:5; capability();
        assert(!g_sl.frameGenerationSupported && !g_sl.frameGenerationMaxMultiplier);
    }
    s_graphicsoptions.raytracingMode=1;
    GraphicsOptions_RayTracingEvent(nullptr,0); assert(s_graphicsoptions.raytracingMode==1);
    for(int index=0;index<2;++index) {
        s_graphicsoptions.raytracing.curvalue=index;
        GraphicsOptions_RayTracingEvent(nullptr,QM_ACTIVATED);
        assert(s_graphicsoptions.raytracingMode==index*2);
    }
    for(limit=2;limit<=6;++limit) {
        menuslider_s slider={5.8f}; GraphicsOptions_FGMultiplierEvent(&slider,QM_ACTIVATED);
        assert(slider.curvalue==limit);
        slider.curvalue=1; GraphicsOptions_FGMultiplierEvent(&slider,QM_ACTIVATED); assert(slider.curvalue==2);
        slider.curvalue=2.6f; GraphicsOptions_FGMultiplierEvent(&slider,QM_ACTIVATED); assert(slider.curvalue==std::min(3,limit));
    }
    puts("PASS: production lighting/multiplier callbacks; SDK capability, request bounds, Off, unsupported presentation and API failure policy");
}
'''
    with tempfile.TemporaryDirectory(prefix='vq3e-mfg-') as folder:
        src, exe = Path(folder)/'check.cpp', Path(folder)/'check.exe'
        src.write_text(fixture)
        sdk = ROOT/'build-widescreen/deps/streamline-sdk-v2.12.0/include'
        subprocess.run([compiler,'-std=c++17','-O2','-I'+str(sdk),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)


def live(path, multiplier):
    text = path.read_text(errors='replace')
    check_presentations(text)
    assert 'PT_MFG_COMPLETE' in text
    assert f'configured {multiplier}x' in text
    counts = []
    for phase in ('BASELINE','STATIC','MOVING'):
        block = text.split('PT_FG_'+phase+'\n',1)[1].split('PT_FG_',1)[0]
        m = STATS.search(block)
        counts.append((int(m[3]),int(m[4])))
    for a,b in zip(counts,counts[1:]):
        ratio = (b[1]-a[1])/(b[0]-a[0])
        assert abs(ratio-multiplier)<.25, (ratio,multiplier)
    print(f'PASS: {multiplier}x sustained static/moving presentations; focused, SDK status OK, complete shutdown')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='C:/msys64/ucrt64/bin/g++.exe')
    parser.add_argument('--log',type=Path)
    parser.add_argument('--multiplier',type=int,choices=range(2,7),default=2)
    args=parser.parse_args()
    offline(args.compiler)
    if args.log: live(args.log,args.multiplier)
