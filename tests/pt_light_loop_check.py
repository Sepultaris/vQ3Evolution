"""Compare original and unified production light loops without GPU submissions."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SHADERS=ROOT/'code/renderer_vulkan/shaders'


def cpp(source):
    source=source.replace('.rgb','.xyz').replace('pc.parameters.y','pc.parameters.xyz.y')
    source=source.replace('skyEnvironment.z','skyEnvironment.xyz.z')
    source=re.sub(r'(?:inout|out) (\w+) (\w+)',r'\1 &\2',source)
    return re.sub(r'(?<![\w.])(\d+\.\d*(?:[eE][+-]?\d+)?)(?![\w.])',r'\1f',source)


def run(compiler):
    source=(SHADERS/'pt_integrator.glsl').read_text()
    original=source.split('#include "pt_light_loop.glsl"\n#else\n',1)[1].split('\n#endif\n',1)[0]
    compact=(SHADERS/'pt_light_loop.glsl').read_text()
    with tempfile.TemporaryDirectory(prefix='pt-light-loop-') as folder:
        folder=Path(folder)
        (folder/'pt_light_loop_original.inc').write_text(cpp(original))
        (folder/'pt_light_loop_compact.inc').write_text(cpp(compact))
        (folder/'pt_penumbra.inc').write_text(cpp((SHADERS/'pt_penumbra.glsl').read_text()))
        for flags in (['-O2'],['-O3','-ffast-math']):
            exe=folder/'light-loop.exe'
            subprocess.run([compiler,'-std=c++17',*flags,'-Wall','-Wextra','-Werror',str(ROOT/'tests/pt_light_loop_fixture.cpp'),'-I',str(folder),'-o',str(exe)],check=True,timeout=30)
            subprocess.run([str(exe)],check=True,timeout=60)


class LightLoopTests(unittest.TestCase):
    def test_confirmed_default_retains_reference_and_safe_fallback(self):
        cvars=(ROOT/'code/renderer_vulkan/tr_cvar.c').read_text()
        self.assertIn('Cvar_Get("r_pathTracingLightLoop", "1", CVAR_CHEAT)',cvars)
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('if (requested & 16) return lighting_pipeline_select(requested & 15);',native)
        self.assertIn('Path tracing unified light loop: requested %d, active %d',native)
        self.assertIn('#ifdef PT_UNIFIED_LIGHTS',(SHADERS/'pt_integrator.glsl').read_text())

    def test_both_benchmark_orders_measure_the_named_variants(self):
        for name,phases in (('pt_light_loop.cfg',('REFERENCE_A','OPTIMIZED_A','OPTIMIZED_B','REFERENCE_B')),
                            ('pt_light_loop_confirm.cfg',('OPTIMIZED_A','REFERENCE_A','REFERENCE_B','OPTIMIZED_B'))):
            active=None
            seen=[]
            for line in (ROOT/'tests'/name).read_text().splitlines():
                if line.startswith('set r_pathTracingLightLoop '):
                    active=int(line.split()[-1])
                elif line.startswith('echo PT_PROGRAM_') and not line.endswith('COMPLETE'):
                    phase=line.removeprefix('echo PT_PROGRAM_')
                    self.assertEqual(active,int(phase.startswith('OPTIMIZED')))
                    seen.append(phase)
            self.assertEqual(tuple(seen),phases)

    def test_single_visibility_and_brdf_site_without_function_calls_or_ray_reduction(self):
        source=(SHADERS/'pt_light_loop.glsl').read_text()
        self.assertEqual(source.count('=visibility('),1)
        self.assertEqual(source.count('=evaluateHitBRDF('),1)
        self.assertIn('lightSlot<lightCounts.y+3u',source)
        self.assertIn('lightSlot==1u ? distance:shadowDistance-0.02',source)
        self.assertNotIn('DontInline',source)
        self.assertNotIn('buffer ',source)

    def test_all_lights_and_original_sampling_remain(self):
        source=(SHADERS/'pt_light_loop.glsl').read_text()
        for expression in ('primitive=sampleEmitter()', 'bary.y=root-bary.x', 'l=normalize(pc.sunExposure.xyz)',
                           'point=lightSlot-2u', 'candidateCount=min(lightCounts.w,8u)',
                           'proposedLight(cell,randomFloat(),proposalPDF)', 'randomFloat()*totalWeight<reservoirWeight',
                           'bounce+1<int(pc.sampling.y) ? powerWeight(lightPDF,pdf) : 1'):
            self.assertIn(expression,source)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx',required=True)
    args=parser.parse_args()
    run(args.cxx)
    unittest.main(argv=[__file__])
