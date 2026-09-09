"""Actual compact transport arithmetic and experimental native lifecycle."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest
import re
import statistics
from pt_material_program_check import parse
ROOT=Path(__file__).resolve().parents[1];SHADERS=ROOT/'code/renderer_vulkan/shaders'
def run(compiler):
    original=(SHADERS/'pt_integrator.glsl').read_text()
    original=original[original.index('vec3 pathD, pathS, pathT, pathE,'):original.index('#include "pt_wave.glsl"')]
    original=original.replace('vec3 visibilityAbsorption;','').replace('#endif','')
    original=original.replace('#include "pt_ambient.glsl"','')
    with tempfile.TemporaryDirectory(prefix='pt-transport-') as folder:
        folder=Path(folder);(folder/'pt_transport_reference.inc').write_text(original)
        for flags in (['-O2'],['-O3','-ffast-math']):
            exe=folder/'transport.exe'
            subprocess.run([compiler,'-std=c++17',*flags,'-Wall','-Wextra','-Werror',str(ROOT/'tests/pt_compact_transport_fixture.cpp'),'-I',str(folder),'-o',str(exe)],check=True,timeout=30)
            subprocess.run([str(exe)],check=True,timeout=30)
        exe=folder/'compact-pipeline.exe'
        subprocess.run([compiler,'-x','c','-std=c11','-O2','-Wall','-Wextra','-Werror',str(ROOT/'tests/pt_compact_pipeline_fixture.c'),'-I',str(ROOT/'code/renderer_vulkan'),'-o',str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=30)
class CompactTransportTests(unittest.TestCase):
    def test_no_new_buffers_rng_or_material_changes(self):
        helper=(SHADERS/'pt_compact_transport.glsl').read_text()
        for forbidden in ('randomFloat','texture','rayQuery','binding=','layout('):self.assertNotIn(forbidden,helper)
        self.assertIn('vec3 pathA,pathB;',helper)
        self.assertIn('pathA*=brdf*factor',helper)
    def test_original_and_candidate_keep_transport_sites(self):
        source=(SHADERS/'pt_integrator.glsl').read_text()
        for token in ('resetCompactPath();','compactReflect();','compactTransmit();','compactScatter(brdf,factor);','compactThroughput();'):
            self.assertEqual(source.count(token),1)
        for token in ('pathS+=pathE; pathE=vec3(0);','pathT+=pathE; pathE=vec3(0);','vec3 throughput=pathD+pathS+pathT;'):
            self.assertIn(token,source)
    def test_measured_default_and_safe_dispatch(self):
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('compact_active ? pt.compact_transport_pipeline[(lighting_mode & 4) ? 1 : 0] : pt.lighting_pipelines[lighting_mode]',native)
        self.assertIn('Cvar_Get("r_pathTracingCompactTransport", "1", CVAR_CHEAT)',(ROOT/'code/renderer_vulkan/tr_cvar.c').read_text())
        shutdown=native.split('void vk_pt_shutdown(void)',1)[1]
        self.assertLess(shutdown.index('for (int i = 0; i < 2; ++i)'),shutdown.index('qvkDestroyPipelineLayout'))

    def test_alias_pdf_keeps_compact_transport_eligible(self):
        helper=(ROOT/'code/renderer_vulkan/pt_compact_transport.h').read_text()
        self.assertIn('(mode != 57 && mode != 61)',helper)
        self.assertIn('uint32_t alias_variant = (mode & 4u) ? 1u : 0u;',helper)
        self.assertIn('alias_variant ? VK_TRUE : VK_FALSE',helper)
    def test_benchmark_roles_and_quality_preserved(self):
        for suffix,expected in [('',[0,1,1,0]),('_confirm',[1,0,0,1])]:
            active=None;seen=[]
            for line in (ROOT/('tests/pt_compact_transport'+suffix+'.cfg')).read_text().splitlines():
                if line.startswith('set r_pathTracingCompactTransport '):active=int(line.split()[-1])
                if line.startswith('echo PT_PROGRAM_') and not line.endswith('COMPLETE'):seen.append(active)
                for quality in ('Samples','Bounces','Exposure'):
                    self.assertFalse(line.startswith('set r_pathTracing'+quality+' '))
            self.assertEqual(seen,expected)
if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx')
    parser.add_argument('--log',type=Path)
    parser.add_argument('--reverse',action='store_true')
    parser.add_argument('--short',action='store_true')
    args=parser.parse_args()
    if args.log:
        text=args.log.read_text(errors='replace')
        phases=(('OPTIMIZED_A','REFERENCE_A') if args.reverse else ('REFERENCE_A','OPTIMIZED_A')) if args.short else \
            (('OPTIMIZED_A','REFERENCE_A','REFERENCE_B','OPTIMIZED_B') if args.reverse else ('REFERENCE_A','OPTIMIZED_A','OPTIMIZED_B','REFERENCE_B'))
        rows=parse(text,phases=phases)
        active=None
        for line in text.splitlines():
            if line.startswith('PT_COMPACT_TRANSPORT enabled='):active=int(line.split('enabled=')[1].split()[0])
            if line.startswith('PT_PROGRAM_') and not line.endswith('COMPLETE'):
                if active!=int('OPTIMIZED' in line):raise ValueError('Actual shader does not match phase')
        views=re.findall(r'\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)',text)
        if not views or any(abs(int(v)-target)>8 for v,target in zip(views[0],(-1510,200,50,0))):raise ValueError('Camera not verified')
        for phase,values in rows.items():
            frames=sorted(row['frame'] for row in values)
            print(f"{phase}: {len(values)} settled samples; frame {statistics.median(frames):.3f} ms, p95 {frames[int(.95*(len(frames)-1))]:.3f} ms; tracing {statistics.median(row['trace'] for row in values):.3f} ms")
        a=statistics.median(row['trace'] for name in phases if name.startswith('REFERENCE') for row in rows[name])
        b=statistics.median(row['trace'] for name in phases if name.startswith('OPTIMIZED') for row in rows[name])
        print(f"Tracing change: {(b/a-1)*100:+.2f}% (negative is faster; not broad validation)")
    else:
        if not args.cxx:parser.error('--cxx is required for native fixtures')
        run(args.cxx);unittest.main(argv=[__file__])
