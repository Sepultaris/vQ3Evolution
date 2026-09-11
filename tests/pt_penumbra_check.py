"""Execute production source-extent math; check live console/history wiring."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'code/renderer_vulkan'

def check(cxx):
    source=(R/'shaders/pt_penumbra.glsl').read_text()
    body=re.sub(r'(?:inout|out) (\w+) (\w+)',r'\1 &\2',source)
    body=re.sub(r'(?<![\w.])(\d+\.\d*)(?![\w.])',r'\1f',body)
    with tempfile.TemporaryDirectory(prefix='pt-penumbra-') as tmp:
        p=Path(tmp);(p/'pt_penumbra.inc').write_text(body)
        exe=p/'penumbra.exe'
        subprocess.run([cxx,'-O2','-std=c++17','-Wall','-Wextra','-Werror',
                        str(ROOT/'tests/pt_penumbra_fixture.cpp'),'-I',tmp,'-o',str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=30)
    cvars=(R/'tr_cvar.c').read_text();native=(R/'vk_pathtrace.c').read_text()
    for name,limit in [('SunAngle',20),('LightRadius',64)]:
        name='r_pathTracing'+name
        assert f'Cvar_Get("{name}", "0", CVAR_ARCHIVE)' in cvars
        assert f'Cvar_CheckRange({name}, 0.0f, {limit}.0f, qfalse)' in cvars
        assert f'extern cvar_t* {name};' in (R/'tr_cvar.h').read_text()
        assert name not in (ROOT/'code/q3_ui/ui_video.c').read_text()
    for index,name in [(2,'sun_angle'),(3,'light_radius')]:
        assert native.index('pt.frozen = r_pathTracingReference->integer != 0;')<native.index(f'lights->sky_environment[{index}] = {name};')
        assert f'radiance_hash = hash_bytes(radiance_hash, &{name}, sizeof({name}));' in native
    assert 'radiance_hash = hash_bytes(radiance_hash, &sun_scale, sizeof(sun_scale));' in native
    assert 'ambient_changed || penumbra_changed' in native
    assert 'M_PI / 360.0' in native, 'Full angular diameter in degrees -> half-angle radians'
    assert 'penumbra: sun angle %.3f degrees, sun scale %.3f, light radius %.3f units' in native
    for file in ['pt_integrator.glsl','pt_light_loop.glsl','pt_fog_lighting.glsl']:
        text=(R/'shaders'/file).read_text()
        assert 'sampleSunPenumbra(' in text and 'samplePointPenumbra(' in text
        assert 'shadowDistance-0.02' in text
    assert 'if(skyEnvironment.w>0) return power*(pointPositions[i].w/d2+0.000001)' in (R/'shaders/pt_rr_sampling.glsl').read_text()
    for forbidden in ('visibility(', 'texture(', 'layout(', 'rayQuery'):
        assert forbidden not in source
    print('PASS: live archived console controls, zero defaults, all lighting paths, finite-source proposal support and RR/native/reference history resets')

def gpu(directory):
    from PIL import Image
    label=directory.name.removeprefix('performance-')
    summary=json.loads((directory.parent/f'pt-perf-{label}.run.json').read_text(encoding='utf-8-sig'))
    manifest=json.loads((directory/'settings.json').read_text(encoding='utf-8-sig'))
    log=(directory/'baseq3/qconsole.log').read_text(errors='replace')
    assert summary['exitCode']==0 and not summary['timedOut'] and summary['completionMarker']
    assert summary['elapsedSeconds']<=45 and summary['sourceSettingsUnchanged']
    assert summary['validationLayerLoaded'] and not (directory/'validation.log').read_text().strip()
    assert not re.search(r'VUID-|SYNC-HAZARD|debugUtilsMessengerCallback|Unknown command',log,re.I)
    assert 'Ray Reconstruction: supported 1, enabled 1' in log
    assert 'Ray Reconstruction evaluation failed' not in log
    values=re.findall(r'Path tracing penumbra: sun angle ([\d.]+) degrees, sun scale ([\d.]+), light radius ([\d.]+) units',log)
    assert [tuple(map(float,v)) for v in values]==[(0,1,0),(0,1,8),(2,1,8),(0,1,0)],values
    resets=[int(v) for v in re.findall(r'Temporal reset causes: renderer \d+, invalid \d+, lights (\d+)',log)]
    assert len(resets)==4 and all(b>a for a,b in zip(resets,resets[1:])),resets
    for key,value in [('r_dlss','5'),('r_pathTracingSamples','2'),('r_pathTracingAdaptive','0'),
                      ('r_dlssFrameGeneration','0'),('r_dlssNeuralRendering','0')]:
        assert manifest['settings'][key]==value
    for name in ('hard','point','sun','restored'):
        path=directory/f'baseq3/screenshots/penumbra_{name}.tga'
        Image.open(path).save(path.with_suffix('.png'))
    print('PASS: live hard/point/sun/restored GPU run, history resets, clean Vulkan, fixed quality and saved settings untouched; inspect images separately')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--cxx',required=True);p.add_argument('--gpu-run',type=Path)
    a=p.parse_args();check(a.cxx)
    if a.gpu_run:gpu(a.gpu_run)
