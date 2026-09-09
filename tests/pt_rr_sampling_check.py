"""Execute production reservoir math and check RR-only scheduling/storage contracts."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SHADERS=ROOT/'code/renderer_vulkan/shaders'

class SamplingTests(unittest.TestCase):
    def test_fixed_two_sample_default_and_optional_adaptive(self):
        source=(ROOT/'code/renderer_vulkan/tr_cvar.c').read_text()
        for name in ('r_dlssRayReconstruction','r_pathTracingLightReuse'):
            self.assertIn(f'ri.Cvar_Get("{name}", "1",',source)
        self.assertIn('ri.Cvar_Get("r_pathTracingSamples", "2", CVAR_ARCHIVE)',source)
        self.assertIn('ri.Cvar_Get("r_pathTracingAdaptive", "0", CVAR_ARCHIVE)',source)
        self.assertIn('ri.Cvar_CheckRange(r_pathTracingAdaptive, 0, 1, qtrue)',source)

    def test_disabled_adaptive_never_reads_reduced_pixel_budget(self):
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('(r_pathTracingAdaptive->integer ? 2u : 0u)',native)
        shader=(SHADERS/'pt_integrator.glsl').read_text()
        self.assertIn('float sampleCount=pc.sampling.x;',shader)
        self.assertIn('if(pc.sunRadiance.w>=2) sampleCount=clamp(rrAdaptive[index].w,1,pc.sampling.x);',shader)

    def test_full_support_fresh_visibility_and_no_spatial_feedback(self):
        source=(SHADERS/'pt_rr_sampling.glsl').read_text()
        self.assertIn('+0.000001)',source)
        self.assertIn('previous.w<8',source)
        self.assertIn('rrOldTemporal[old]',source)
        self.assertNotIn('rrOldSpatial',source)
        source=(SHADERS/'pt_light_loop.glsl').read_text()
        self.assertLess(source.index('rrPrimaryLight'),source.index('vec3 transmittance=visibility'))
        source=(SHADERS/'pt_integrator.glsl').read_text()
        spatial=source[source.index('#ifdef PT_RR_SPATIAL\nvoid main()'):source.index('#elif defined(PT_STAGED_PASS)')]
        self.assertNotIn('rrSpatial[other]',spatial)
        self.assertIn('rrTemporal[other]',spatial)

    def test_budget_uses_previous_evidence_and_actual_normalization(self):
        source=(SHADERS/'pt_rr_sampling.glsl').read_text()
        self.assertIn('rrOldAdaptive[old]',source)
        self.assertIn('adaptive && safe && matched && lightSelection.w==0',source)
        self.assertIn('oldNdc-currentNdc',source)
        self.assertIn('trusted && k<4',source)
        self.assertIn('rrMovingHistorySafe',source)
        self.assertIn('rrSampleBudget(pc.sampling.x,moments,budgetSafe)',source)
        source=(SHADERS/'pt_integrator.glsl').read_text()
        self.assertIn('diffuse/=sampleCount;',source)
        self.assertIn('reflection/=sampleCount;',source)
        self.assertIn('sampleNumber=uint(pc.sampling.z)*uint(pc.sampling.x)+uint(sampleIndex)',source)
        self.assertIn('roughness>=0.35 && properties.y<0.3',source)

    def test_aliasing_has_disjoint_buffers_and_resets(self):
        source=(ROOT/'code/renderer_vulkan/pt_ray_reconstruction.h').read_text()
        self.assertIn('&pt.moments[i], &pt.moments[i ^ 1], &pt.light_change',source)
        self.assertIn('&pt.history_color[i], &pt.history_color[i ^ 1]',source)
        self.assertNotIn('qvkCreateBuffer',source)
        source=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('if (rr_sampling != pt_rr.sampling_flags) pt.temporal_valid = qfalse;',source)
        self.assertIn('if (c[27] != 0 && !pt_rr.frame_ready)',source)

    def test_failure_produces_display_before_fallback(self):
        source=(ROOT/'code/renderer_vulkan/pt_ray_reconstruction.h').read_text()
        block=source[source.index('if (!vk_sl_evaluate_ray_reconstruction'):]
        self.assertLess(block.index('pt_rr.fallback'),block.index('pt_rr.frame_ready = qfalse'))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx',required=True)
    parser.add_argument('--budget-run',type=Path)
    parser.add_argument('--compare',nargs=2,type=Path)
    parser.add_argument('--spirv-dis',type=Path)
    args=parser.parse_args()
    result=unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(SamplingTests))
    if not result.wasSuccessful(): raise SystemExit(1)
    with tempfile.TemporaryDirectory(prefix='pt-rr-sampling-') as directory:
        folder=Path(directory)
        math=(SHADERS/'pt_reservoir_math.glsl').read_text()
        math=re.sub(r'inout (\w+) (\w+)',r'\1 &\2',math)
        # GLSL numeric literals are floats; C++ requires suffixes for std::max.
        math=math.replace('max(', 'std::max<float>(')
        (folder/'rr_math.inc').write_text(math)
        exe=folder/'check.exe'
        subprocess.run([args.cxx,'-std=c++17','-O2',str(ROOT/'tests/pt_rr_sampling_fixture.cpp'),'-I',directory,'-o',str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=30)
    if args.spirv_dis:
        dis=subprocess.run([str(args.spirv_dis),str(SHADERS/'Compiled/pt_rr_guides.cspv')],
                           capture_output=True,text=True,check=True,timeout=30).stdout
        for name in ('guidePositions','guideNormals','guideAlbedos','motionPositions','motionNormals',
                     'shadingGuides','reflectionGuides','reflectionMotion','lightChange','transmissionGuides'):
            assert not re.search(r'OpName\s+\S+\s+"'+name+'"',dis),name+' still retained in RR guide shader'
        print('PASS: optimized RR guide binary has no native-denoiser output buffers')
    if args.compare:
        from pt_ray_reconstruction_check import read_run
        values=[read_run(p) for p in args.compare]
        def identity(m):
            return m['sourceSha256'],m['config'],{k:v for k,v in m['settings'].items()
                if k not in ('r_pathTracingLightReuse','r_pathTracingAdaptive')}
        assert identity(values[0][0])==identity(values[1][0]),'Quality/settings/scene mismatch'
        print(json.dumps([v[1] for v in values],indent=2))
    if args.budget_run:
        import numpy as np
        from PIL import Image
        p=args.budget_run
        summary=json.loads((p.parent/('pt-perf-'+p.name.removeprefix('performance-')+'.run.json')).read_text(encoding='utf-8-sig'))
        assert summary['completionMarker'] and not summary['timedOut'] and summary['sourceSettingsUnchanged']
        assert summary['validationLayerLoaded'] and not (p/'validation.log').read_text().strip()
        log=(p/'baseq3/qconsole.log').read_text(errors='replace')
        assert '[debugUtilsMessengerCallback]' not in log
        assert 'RR sampling: light reuse 1, adaptive 1, ceiling 4 spp' in log
        fractions=[]
        for name in ('stationary','motion'):
            a=np.asarray(Image.open(p/'baseq3/screenshots'/f'rr_budget_{name}.jpg').convert('RGB'),dtype=float)[:-100]
            r,g,b=np.moveaxis(a,-1,0)
            green=(g>150)&(g>r+70)&(b<90)
            red=(r>150)&(r>g+70)&(b<90)
            fraction=float(green.sum()/max((green|red).sum(),1))
            fractions.append(fraction)
            print(f'Budget capture {name}: {fraction:.2%} half-budget pixels')
        assert fractions[0]>.2 and fractions[1]<.01,'Stable/moving budget protection failed'
