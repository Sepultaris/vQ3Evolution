"""Exercise the production NR first-use initialization and cleanup policy."""
import argparse
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
source=(ROOT/'code/renderer_vulkan/vk_dlssnr.cpp').read_text()
body=source[source.index('void releaseFeature()'):source.index('void resetModules()')]
body+=source[source.index('extern "C" void vk_dlssnr_configure('):source.index('extern "C" qboolean vk_dlssnr_evaluate(')]
fixture=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
using NVSDK_NGX_Result=int; using VkCommandBuffer=int;
using qboolean=bool;
struct NVSDK_NGX_Parameter {}; struct NVSDK_NGX_Handle {};
const bool qfalse=false,qtrue=true;
const int VK_NULL_HANDLE=0, PRINT_WARNING=1, PRINT_ALL=0, NVSDK_NGX_Version_API=1;
const int kFeatureDlssNr=18;
const unsigned long long kStreamlineTemporaryAppId=1;
#define NVSDK_NGX_FAILED(r) ((r)<0)
static int failure,inits,shutdowns,destroys,creates;
static NVSDK_NGX_Parameter params;
static NVSDK_NGX_Handle feature;
static std::wstring executableDirectory() { return L"fixture"; }
static void print(int,const char *,...) {}
static void set(const char *,const char *) {}
static struct { void(*Printf)(int,const char *,...); void(*Cvar_Set)(const char *,const char *); } ri={print,set};
static int init(int,unsigned long long,const wchar_t *,int,int,int,int,int,int,const void *) { ++inits; return failure==1?-1:0; }
static int caps(NVSDK_NGX_Parameter **p) { *p=failure==2?nullptr:&params; return failure==2?-1:0; }
static int populate(int,NVSDK_NGX_Parameter *p) { assert(p==&params); return failure==3?-1:0; }
static int destroy(NVSDK_NGX_Parameter *p) { assert(p==&params); ++destroys; return 0; }
static int shutdown(int,int device) { assert(device==1); ++shutdowns; return 0; }
static int release(int,NVSDK_NGX_Handle *p) { assert(p==&feature); return 0; }
static int create(int,int,int,NVSDK_NGX_Parameter *p,NVSDK_NGX_Handle **h) { assert(p==&params); ++creates; *h=&feature; return 0; }
struct State {
    bool runtimeInitialized=false,attached=true,failed=false,evaluationLogged=false;
    int init=1,populate=1,release=1,shutdown=1,create=1,device=1,instance=1,physicalDevice=1,getInstanceProcAddr=1,getDeviceProcAddr=1;
    uint32_t width=1920,height=1080; int mode=0;
    NVSDK_NGX_Parameter *parameters=nullptr; NVSDK_NGX_Handle *feature=nullptr;
    decltype(&::init) bridgeInit=::init; decltype(&caps) getCapabilities=caps;
    decltype(&::populate) bridgePopulate=::populate; decltype(&destroy) destroyParameters=destroy;
    decltype(&::shutdown) bridgeShutdown=::shutdown; decltype(&::release) bridgeRelease=::release;
    decltype(&::create) bridgeCreate=::create;
} g_nr;
static void setCreationParameters() { assert(g_nr.runtimeInitialized && g_nr.parameters); }
'''+body+r'''
int main() {
    for(int mode=0;mode<=3;++mode) for(failure=0;failure<=3;++failure) {
        g_nr=State{}; inits=shutdowns=destroys=creates=0;
        vk_dlssnr_configure(mode,1920,1080);
        bool ready=vk_dlssnr_prepare(1);
        assert(ready==(mode!=0 && failure==0));
        assert(inits==(mode!=0));
        if(ready) {
            assert(vk_dlssnr_prepare(1) && inits==1 && creates==1);
            vk_dlssnr_configure(0,1920,1080);
            assert(!vk_dlssnr_prepare(1) && inits==1);
            vk_dlssnr_configure(mode,1920,1080);
            assert(vk_dlssnr_prepare(1) && inits==1 && creates==2);
        }
        shutdownRuntime(); shutdownRuntime();
        assert(shutdowns==int(mode!=0 && failure!=1));
        assert(destroys==int(mode!=0 && (failure==0 || failure==3)));
    }
    g_nr=State{}; inits=0; g_nr.mode=1;
    assert(!vk_dlssnr_prepare(0) && inits==0);
    puts("PASS: NR off/no command buffer never initializes; first-use/reuse, failure unwind and exactly-once shutdown");
}
'''
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx',required=True);a=p.parse_args()
attach=source[source.index('extern "C" qboolean vk_dlssnr_attach'):source.index('extern "C" void vk_dlssnr_shutdown')]
assert 'g_nr.bridgeInit(g_nr.init' not in attach
assert 'vk_sl_verify_nvidia_signature' in attach
with tempfile.TemporaryDirectory(prefix='nr-lazy-') as folder:
    folder=Path(folder);(folder/'test.cpp').write_text(fixture)
    subprocess.run([a.cxx,'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(folder/'test.cpp'),'-o',str(folder/'test.exe')],check=True,timeout=30)
    subprocess.run([str(folder/'test.exe')],check=True,timeout=10)
