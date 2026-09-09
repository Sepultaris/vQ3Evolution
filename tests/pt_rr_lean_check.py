"""RR combined transport math, unchanged native shader, and direct-output routing.

Arithmetic checks do not establish performance; compare matched stationary game
runs separately, with background texture upscaling idle and frame generation off.
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import statistics
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SHADERS=ROOT/'code/renderer_vulkan/shaders'

class LeanTests(unittest.TestCase):
    def test_only_rr_uses_combined_transport(self):
        users=[p.name for p in SHADERS.glob('*.comp') if '#define PT_RR_COMBINED 1' in p.read_text()]
        self.assertEqual(users,['pt_rr_trace.comp'])
        integrator=(SHADERS/'pt_integrator.glsl').read_text()
        self.assertIn('PT_RR_COMBINED',integrator)
        self.assertIn('pt_rr_transport.glsl',integrator)
        helper=(SHADERS/'pt_rr_transport.glsl').read_text()
        for forbidden in ('randomFloat','rayQuery','texture','radianceD','radianceS','radianceT','radianceE'):
            self.assertNotIn(forbidden,helper)

    def test_fused_output_keeps_guides_fallback_and_moments(self):
        source=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('if (pt_rr.frame_ready) rr_pack(cmd, c, rr_trace);',source)
        self.assertIn('pt_rr.direct_profile_frame = rr_trace;',source)
        self.assertIn('if (pt_rr.direct_profile_frame) ms[4] = ms[5] = 0;',source)
        self.assertIn('pt_rr.direct_profile_frame = qfalse;',source)
        source=(ROOT/'code/renderer_vulkan/pt_ray_reconstruction.h').read_text()
        output=source.split('static void rr_pack(',1)[1].split('qboolean vk_pt_rr_evaluate',1)[0]
        self.assertLess(output.index('memcpy(pt_rr.push'),output.index('if (!direct_output)'))
        self.assertLess(output.index('rr_barrier(cmd)'),output.index('if (!direct_output)'))
        self.assertEqual(output.count('rr_barrier(cmd)'),2)
        self.assertIn('qvkCmdDispatch(cmd,',output)
        self.assertIn('pt_rr.fallback, pt_rr.push',source)
        for shader in ('pt_rr_pack.comp','pt_integrator.glsl'):
            self.assertIn('#include "pt_rr_output.glsl"',(SHADERS/shader).read_text())
        helper=(SHADERS/'pt_rr_output.glsl').read_text()
        for token in ('if(pc.sunRadiance.w>=2)','rrAdaptive[index]=','clamp(color,0,65504)','isnan','isinf'):
            self.assertIn(token,helper)
        for forbidden in ('toneMap(', 'pow(', 'imageLoad(rrOutput'):
            self.assertNotIn(forbidden,helper)

def run(cxx,sdk,baseline):
    with tempfile.TemporaryDirectory(prefix='pt-rr-lean-') as tmp:
        exe=Path(tmp)/'transport.exe'
        for options in (['-O2'],['-O3','-ffast-math']):
            subprocess.run([cxx,'-std=c++17',*options,'-Wall','-Wextra','-Werror',str(ROOT/'tests/pt_rr_transport_fixture.cpp'),'-o',str(exe)],check=True,timeout=30)
            subprocess.run([str(exe)],check=True,timeout=30)
        if baseline:
            # Preprocess the old integrator using the unchanged current shared
            # includes, then compare every token of the native compact body.
            validator=str(sdk/'Bin/glslangValidator.exe')
            old=Path(tmp)/'old.comp'
            old.write_text('#version 460\n#extension GL_GOOGLE_include_directive : require\n#define PT_COMPACT_TRANSPORT 1\n#define PT_MATERIAL_CACHE 1\n#define PT_BRDF_REUSE 1\n#define PT_UNIFIED_LIGHTS 1\n#define PT_WORKGROUP_SPECIALIZATION 1\n'+baseline.read_text())
            a=subprocess.check_output([validator,'-E','-I'+str(SHADERS),str(old)],text=True)
            b=subprocess.check_output([validator,'-E',str(SHADERS/'pt_compact_transport.comp')],text=True)
            tokens=lambda text: re.findall(r'\w+|[^\s\w]',re.sub(r'^#.*$','',text,flags=re.M))
            assert tokens(a)==tokens(b),'Native compact transport changed'
            print('PASS: native compact shader token-identical to pre-change source')

def compare(paths,preview):
    from PIL import Image,ImageChops,ImageStat
    manifests=[]; images=[]; results=[]; cameras=[]
    for path in paths:
        manifest=json.loads((path/'settings.json').read_text(encoding='utf-8-sig'))
        summary=json.loads((path.parent/('pt-perf-'+path.name.removeprefix('performance-')+'.run.json')).read_text(encoding='utf-8-sig'))
        text=(path/'baseq3/qconsole.log').read_text(errors='replace')
        assert manifest['settings']['r_dlssFrameGeneration']=='0'
        assert manifest['settings']['r_dlss']=='5'
        assert manifest['settings']['r_dlssRayReconstruction']=='1'
        assert manifest['settings']['r_pathTracingSamples']=='2'
        assert manifest['settings']['r_pathTracingAdaptive']=='0'
        assert 'Frame Generation: enabled 0, viewport active 0' in text
        assert 'Ray Reconstruction: supported 1, enabled 1' in text
        assert 'PT_PROGRAM_COMPLETE' in text
        views=re.findall(r'^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)$',text,re.M)
        assert len(views)==2,views
        cameras.append(views)
        rows=[{k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)',line)} for line in text.splitlines() if line.startswith('PT_PROFILE ')]
        assert len(rows)>=20
        rows=rows[3:-3]
        clean=summary['exitCode']==0 and not summary['timedOut'] and views[0]==views[1]
        results.append(dict(name=path.name,accepted=clean,samples=len(rows),ms={k:statistics.median(r[k] for r in rows) for k in rows[0]}))
        assert summary['sourceSettingsUnchanged'] and summary.get('sharedSourceSettingsUnchanged',True)
        manifests.append(manifest)
        images.append(Image.open(path/'baseq3/screenshots/rr_lean_stationary.tga').convert('RGB'))
    for key in ('settings','sourceSha256','sharedSourceSha256','executableSha256'):
        assert manifests[0][key]==manifests[1][key],key
    assert (paths[0]/'baseq3/pt_rr_lean.cfg').read_bytes()==(paths[1]/'baseq3/pt_rr_lean.cfg').read_bytes(),'Scenario changed'
    assert cameras[0]==cameras[1],cameras
    print(json.dumps(results,indent=2))
    print('World RGB MAE:',ImageStat.Stat(ImageChops.difference(*images).crop((0,90,images[0].width,images[0].height-120))).mean)
    if preview:
        canvas=Image.new('RGB',(1280,360))
        for i,img in enumerate(images): canvas.paste(img.resize((640,360)),(i*640,0))
        canvas.save(preview)
    if not all(r['accepted'] for r in results) or cameras[0][0]!=cameras[0][1]:
        print('DIAGNOSTIC ONLY: timed-out or moving-camera runs cannot establish an accepted performance gain.')
        return
    print('Trace change: %.2f%%; frame change: %.2f%%' % tuple((results[1]['ms'][k]/results[0]['ms'][k]-1)*100 for k in ('trace','frame')))

def gpu_check(cxx,sdk):
    # Reuse the existing bounded, headless image/readback harness. These are
    # transport arithmetic cases on real GPU hardware, not rendered-map tests.
    with tempfile.TemporaryDirectory(prefix='pt-rr-transport-gpu-') as tmp:
        tmp=Path(tmp); binaries=[]
        for name,defines in (('zero',['-DPT_RR_EXPECT_ZERO=1']),('actual',[]),('bad',['-DPT_RR_EXPECT_ZERO=1','-DPT_RR_CORRUPT=1'])):
            out=tmp/(name+'.spv'); binaries.append(out)
            subprocess.run([str(sdk/'Bin/glslangValidator.exe'),'--target-env','vulkan1.2','-V',*defines,'-I'+str(SHADERS),str(ROOT/'tests/pt_rr_transport_gpu.comp'),'-o',str(out)],check=True,timeout=20)
            subprocess.run([str(sdk/'Bin/spirv-val.exe'),'--target-env','vulkan1.2',str(out)],check=True,timeout=20)
        exe=tmp/'check.exe'
        subprocess.run([cxx,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers',str(ROOT/'tests/pt_rr_post_gpu.cpp'),'-I',str(sdk/'Include'),'-L',str(sdk/'Lib'),'-l:vulkan-1.lib','-o',str(exe)],check=True,timeout=30)
        validation=tmp/'validation.log'
        (tmp/'vk_layer_settings.txt').write_text('khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'+f'khronos_validation.log_filename = {validation.as_posix()}\n'+'khronos_validation.report_flags = error,warn\nkhronos_validation.validate_sync = true\n')
        env=dict(os.environ,VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',VK_LAYER_PATH=str(sdk/'Bin'),VK_LAYER_SETTINGS_PATH=str(tmp))
        subprocess.run([str(exe),str(binaries[0]),str(binaries[1])],check=True,timeout=25,env=env)
        assert validation.exists() and not validation.read_text().strip()
        result=subprocess.run([str(exe),str(binaries[0]),str(binaries[2])],timeout=25,env=env)
        assert result.returncode==1,'Negative GPU control failed to detect bad transport'
        assert validation.exists() and not validation.read_text().strip()
        print('PASS: actual production GLSL transport agrees on GPU; negative control detected; core/sync validation clean')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cxx')
    p.add_argument('--sdk',type=Path)
    p.add_argument('--baseline',type=Path)
    p.add_argument('--runs',type=Path,nargs=2)
    p.add_argument('--preview',type=Path)
    p.add_argument('--gpu',action='store_true')
    a=p.parse_args()
    if a.runs:
        compare(a.runs,a.preview)
        raise SystemExit(0)
    if not a.cxx or not a.sdk: p.error('--cxx and --sdk are required for offline checks')
    run(a.cxx,a.sdk,a.baseline)
    if a.gpu: gpu_check(a.cxx,a.sdk)
    unittest.main(argv=[__file__])
