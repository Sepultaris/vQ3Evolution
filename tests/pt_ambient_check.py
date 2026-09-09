"""Console ambient-fill wiring and optional isolated live OFF/ON/OFF verification."""
import argparse
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'code/renderer_vulkan'

def check():
    native=(R/'vk_pathtrace.c').read_text();shader=(R/'shaders/pt_integrator.glsl').read_text()
    cvars=(R/'tr_cvar.c').read_text();helper=(R/'shaders/pt_ambient.glsl').read_text()
    assert 'Cvar_Get("r_pathTracingAmbient", "0", CVAR_ARCHIVE)' in cvars
    assert 'Cvar_CheckRange(r_pathTracingAmbient, 0.0f, 2.0f, qfalse)' in cvars
    assert 'extern cvar_t* r_pathTracingAmbient;' in (R/'tr_cvar.h').read_text()
    assert 'r_pathTracingAmbient' not in (ROOT/'code/q3_ui/ui_video.c').read_text(), 'Console only'
    record=native[native.index('qboolean vk_pt_record('):]
    assert record.index('const qboolean ambient_changed')<record.index('memset(lights, 0, sizeof(*lights))')
    assert record.index('pt.frozen = r_pathTracingReference->integer != 0;')<record.index('lights->sky_environment[1] = ambient;')
    assert 'radiance_hash = hash_bytes(radiance_hash, &ambient, sizeof(ambient));' in record
    assert '(radiance_hash != pt.previous_radiance_hash ? 4u : 0u)' in record
    assert 'geometry_changed || lights_changed || ambient_changed' in record
    assert 'reconstruction->jitter_history_reset[3] = reject_history ? 1 : 0;' in record
    assert 'if(strength<=0) return;' in helper
    for forbidden in ('randomFloat(', 'rayQuery', 'texture(', 'binding=', 'layout('): assert forbidden not in helper
    assert 'addDirect(diffuseBRDF,vec3(strength))' in helper
    assert 'addAmbient(albedo,metallic,skyEnvironment.y)' in shader
    assert 'if(skyEnvironment.y>0) addIncident(vec3(skyEnvironment.y));' in shader
    assert shader.index('if(!fogScatter(fogVolume,fogWeight))')<shader.index('if(skyEnvironment.y>0) addIncident')
    print('PASS: console-only archived live control; existing scalar, no added rays/storage; surface/fog wiring; RR/native/reference history invalidation')

def gpu(directory):
    import numpy as np
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
    assert manifest['config']=='pt_ambient.cfg'
    for key,value in (('r_dlss','5'),('r_dlssFrameGeneration','0'),('r_dlssNeuralRendering','0'),
                      ('r_pathTracingSamples','2'),('r_pathTracingAdaptive','0')):
        assert manifest['settings'][key]==value
    values=re.findall(r'Path tracing ambient fill: ([\d.]+) \(0 disables; exposure ([\d.]+)\)',log)
    assert [float(a) for a,b in values]==[0,.15,0],values
    assert len({b for a,b in values})==1,'Exposure must not change'
    causes=[int(v) for v in re.findall(r'Temporal reset causes: renderer \d+, invalid \d+, lights (\d+)',log)]
    assert len(causes)==3 and causes[0]<causes[1]<causes[2],causes
    images=[]
    for name in ('off','on','restored'):
        im=Image.open(directory/f'baseq3/screenshots/ambient_{name}.tga')
        im.save(directory/f'baseq3/screenshots/ambient_{name}.png')
        images.append(np.asarray(im,dtype=float)[140:680,70:430,:3])
    means=[im.mean() for im in images]
    assert means[1]-means[0]>2,means
    assert abs(means[2]-means[0])<2+(means[1]-means[0])*.1,means
    print('PASS: live OFF/0.15/OFF, unchanged exposure/quality, history resets, clean Vulkan, source settings untouched; wall-region means:',means)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--gpu-run',type=Path)
    a=p.parse_args();check()
    if a.gpu_run:gpu(a.gpu_run)
