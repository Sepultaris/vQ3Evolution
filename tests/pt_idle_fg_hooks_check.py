"""Check the real idle-FG lifecycle policy and completed fixed-settings runs."""
import argparse
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

def run_policy(compiler):
    source=(ROOT/'code/renderer_vulkan/vk_streamline.cpp').read_text()
    body=source[source.index('void unloadIdleFrameGenerationHooks()'):source.index('void clearState(bool unload)')]
    fixture='''#include <cassert>
#include <cstring>
#include <cstdio>
namespace sl { enum class Result { eOk, failed }; const int kFeatureDLSS_G=1000; }
enum { PRINT_WARNING, PRINT_ALL };
static int requested,force,calls,warnings;static bool succeed;
static sl::Result unload(int feature,bool loaded){assert(feature==sl::kFeatureDLSS_G && !loaded);++calls;return succeed?sl::Result::eOk:sl::Result::failed;}
static int cvar(const char *name){if(!std::strcmp(name,"r_dlssFrameGeneration"))return requested;assert(!std::strcmp(name,"r_dlssFGIdleHooks"));return force;}
static void print(int level,const char *,...){warnings+=level==PRINT_WARNING;}
static struct {int (*Cvar_VariableIntegerValue)(const char *);void (*Printf)(int,const char *,...);}ri={cvar,print};
static struct {bool frameGenerationSupported,frameGenerationHooksUnloaded;sl::Result(*setFeatureLoaded)(int,bool);void *dlssGSetOptions,*dlssGGetState;}g_sl;
'''+body+'''
int main(){
    int sentinel;
    for(int flags=0;flags<32;++flags){
        requested=flags&1;force=flags&2;bool supported=flags&4,api=flags&8;succeed=flags&16;
        g_sl={supported,false,api?unload:nullptr,&sentinel,&sentinel};calls=warnings=0;
        unloadIdleFrameGenerationHooks();
        bool attempt=supported&&!requested&&!force;
        bool changed=attempt&&api&&succeed;
        assert(calls==(attempt&&api));assert(warnings==(attempt&&!changed));
        assert(g_sl.frameGenerationSupported==supported);
        assert(g_sl.frameGenerationHooksUnloaded==changed);
        assert(g_sl.dlssGSetOptions==(changed?nullptr:&sentinel));
        assert(g_sl.dlssGGetState==(changed?nullptr:&sentinel));
    }
    std::puts("PASS: 32 production idle-FG policy states; requested FG retained, capability preserved, failed unhook keeps valid interfaces");
}
'''
    with tempfile.TemporaryDirectory(prefix='pt-idle-fg-') as folder:
        folder=Path(folder);src=folder/'policy.cpp';exe=folder/'policy.exe';src.write_text(fixture)
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(src),'-o',str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=30)

def parse(text):
    if text.count('\nPT_HOOKS_BEGIN\n')!=1 or text.count('\nPT_HOOKS_COMPLETE\n')!=1 or 'Path tracer: active 1' not in text:
        raise ValueError('Incomplete idle-hook run')
    body=text.split('\nPT_HOOKS_BEGIN\n',1)[1].split('\nPT_HOOKS_COMPLETE\n',1)[0]
    rows=[]
    for line in body.splitlines():
        if line.startswith('PT_PROFILE '):
            values={k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)',line)}
            if set(values)!={'frame','raster','as','trace','guides','temporal','spatial','post'}:raise ValueError('Incomplete timing row')
            rows.append(values)
    rows=rows[3:-3]
    if len(rows)<80:raise ValueError('Not enough settled timing samples')
    views=re.findall(r'\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)',text)
    if not views or any(abs(int(v)-target)>8 for v,target in zip(views[0],(-1510,200,50,0))):raise ValueError('Initial camera not verified')
    mode=re.search(r'NVIDIA idle FG hooks: unloaded (\d), FG capability retained (\d)',body)
    if not mode or 'Frame Generation: enabled 0,' not in body:raise ValueError('Idle FG state not verified')
    return rows,tuple(map(int,mode.groups()))

class IdleFGTests(unittest.TestCase):
    def test_unhook_after_capability_queries_before_surface(self):
        source=(ROOT/'code/renderer_vulkan/vk_streamline.cpp').read_text()
        check=source.split('extern "C" void vk_sl_check_feature_support(',1)[1].split('extern "C" void *vk_sl_get_instance_proc_addr',1)[0]
        self.assertGreater(check.index('unloadIdleFrameGenerationHooks();'),check.index('"slDLSSGGetState"'))
        native=(ROOT/'code/renderer_vulkan/vk_instance.c').read_text()
        self.assertLess(native.index('    vk_createLogicalDevice();'),native.index('    vk_sl_check_feature_support('))
        self.assertLess(native.index('    vk_sl_check_feature_support('),native.index('vk_createSurfaceImpl();'))
        cvars=(ROOT/'code/renderer_vulkan/tr_cvar.c').read_text()
        self.assertIn('"r_dlssFrameGeneration", "0", CVAR_ARCHIVE | CVAR_LATCH',cvars)
        self.assertIn('"r_dlssFGIdleHooks", "0", CVAR_CHEAT | CVAR_LATCH',cvars)

    def test_no_mid_frame_hook_changes_or_fake_capability(self):
        source=(ROOT/'code/renderer_vulkan/vk_streamline.cpp').read_text()
        self.assertEqual(source.count('g_sl.setFeatureLoaded(sl::kFeatureDLSS_G, false)'),1)
        policy=source.split('void unloadIdleFrameGenerationHooks()',1)[1].split('void clearState',1)[0]
        self.assertNotIn('Cvar_Set',policy);self.assertNotIn('frameGenerationSupported =',policy)
        self.assertIn('if (g_sl.dlssGSetOptions)',source)
        self.assertIn('g_sl.frameGenerationResourcesAllocated && g_sl.freeResources',source)

    def test_reference_switch_precedes_startup_and_is_not_a_quality_override(self):
        runner=(ROOT/'tests/run-pt-performance.ps1').read_text()
        self.assertIn("'+set r_dlssFGIdleHooks 1 ' + $ptArguments",runner)
        self.assertLess(runner.index('if ($KeepIdleFrameGenerationHooks)'),runner.index('$ptProcess = Start-Process'))
        self.assertIn('keepIdleFrameGenerationHooks = [bool]$KeepIdleFrameGenerationHooks',runner)
        self.assertIn("$ptSummary['validationLayerLoaded']",runner)
        self.assertIn("$_.ModuleName -ieq 'VkLayer_khronos_validation.dll'",runner)
        config=(ROOT/'tests/pt_idle_fg_hooks.cfg').read_text()
        for quality in ('r_dlss ', 'r_dlssFrameGeneration ', 'r_pathTracingSamples ', 'r_pathTracingBounces '):self.assertNotIn('set '+quality,config)

    def test_completed_profile_requirements(self):
        row='PT_PROFILE frame=70 raster=0.1 as=0.4 trace=57 guides=1 temporal=1 spatial=2 post=6\n'
        text='\n(-1517 200 50) : 0\nPT_HOOKS_BEGIN\n'+row*100+'Path tracer: active 1\nNVIDIA idle FG hooks: unloaded 1, FG capability retained 1\nFrame Generation: enabled 0, viewport active 0\nPT_HOOKS_COMPLETE\n'
        rows,mode=parse(text);self.assertEqual(len(rows),94);self.assertEqual(mode,(1,1))
        for broken in (text.replace('PT_HOOKS_COMPLETE','timeout'),text.replace(row,'',25),text.replace('200 50','200 120'),text.replace('unloaded 1','unknown 1'),text.replace('enabled 0,','enabled 1,')):
            with self.assertRaises(ValueError):parse(broken)

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--cxx');ap.add_argument('logs',nargs='*',type=Path);args=ap.parse_args()
    if args.cxx:run_policy(args.cxx)
    if args.logs:
        for log in args.logs:
            rows,mode=parse(log.read_text(errors='replace'))
            medians={k:statistics.median(row[k] for row in rows) for k in rows[0]}
            frames=sorted(row['frame'] for row in rows)
            print(log.name,'settled',len(rows),'hooks-unloaded/capability',mode,'medians-ms',medians,'frame-p95',frames[int(.95*(len(frames)-1))])
    else:unittest.main(argv=[__file__])
