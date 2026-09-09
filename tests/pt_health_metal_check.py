"""Health-metal tint-only lookup/source contracts and optional matched GPU runs."""
import argparse
import json
from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1];R=ROOT/'code/renderer_vulkan'

def check():
    native=(R/'vk_pathtrace.c').read_text();shader=(R/'shaders/pt_integrator.glsl').read_text()
    assert 'sizeof("models/powerups/health/")-1' in native
    assert 'm->layers[base_layer].vectors[0][3] = 3' in native
    assert 'm->surface[1] = 0.12f; m->surface[2] = 1;' in native
    assert 'uniformTint=uniformTint || program==3;' in shader
    assert shader.index('uniformTint=uniformTint || program==3;')<shader.index('context.uniformTint=uniformTint;')
    assert 'if(!uniformTint && materials[id].layers[layerIndex].params.z==4)' in shader
    assert 'for(int i=0;!context.uniformTint && i<int(layer.params.w);++i)' in shader
    block=shader.split('if(context.uniformTint) {',1)[1].split('} else sampled=',1)[0]
    assert 'textureLod' in block and 'textureQueryLevels' in block and 'vec2(0.5)' in block
    assert 'sampled.rgb/peak' in block
    for forbidden in ('environmentMaterialUV(', 'sampleTexture(', 'observer-', 'dot(normal'):
        assert forbidden not in block
    assert 'baseColor(hit.primitive,hit.bary,origin)' in shader
    assert 'surfaceProperties(hit.primitive,hit.bary)' in shader
    assert 'baseColor(hit.primitive,hit.bary,pc.originNear.xyz)' in shader
    print('PASS: health base forces existing uniform-pigment lookup, bypasses reflection projection/UV animation, and shares physical shading with guides')

def gpu(before,after):
    from PIL import Image
    manifests=[]
    for directory in (before,after):
        name=directory.name.removeprefix('performance-')
        summary=json.loads((directory.parent/f'pt-perf-{name}.run.json').read_text(encoding='utf-8-sig'))
        manifest=json.loads((directory/'settings.json').read_text(encoding='utf-8-sig'));manifests.append(manifest)
        log=(directory/'baseq3/qconsole.log').read_text(errors='replace')
        assert summary['exitCode']==0 and not summary['timedOut'] and summary['completionMarker']
        assert summary['sourceSettingsUnchanged'] and summary['elapsedSeconds']<=45
        assert manifest['map']=='q3dm0' and manifest['config']=='pt_health_metal.cfg'
        for key,value in (('r_dlss','5'),('r_dlssFrameGeneration','0'),('r_dlssNeuralRendering','0'),
                          ('r_pathTracingSamples','2'),('r_pathTracingAdaptive','0')):
            assert manifest['settings'][key]==value
        assert 'Ray Reconstruction: supported 1, enabled 1' in log
        assert 'Ray Reconstruction evaluation failed' not in log
        for shot in ('yellow','orange','orange_angle'):
            path=directory/f'baseq3/screenshots/health_{shot}.tga'
            assert path.stat().st_size>1920*1080*3
            Image.open(path).save(path.with_suffix('.png'))
        if directory==after:
            assert summary['validationLayerLoaded'] and not (directory/'validation.log').read_text().strip()
            assert not re.search(r'VUID-|SYNC-HAZARD|debugUtilsMessengerCallback',log)
    assert manifests[0]['settings']==manifests[1]['settings']
    assert manifests[0]['sourceSha256']==manifests[1]['sourceSha256']
    assert manifests[0]['rendererSha256']!=manifests[1]['rendererSha256']
    print('PASS: matched saved DLAA/RR/two-sample before/after captures, clean validation, automatic shutdown, settings untouched; inspect pickup appearance')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--before',type=Path);p.add_argument('--after',type=Path)
    a=p.parse_args();check()
    if a.before or a.after:
        assert a.before and a.after
        gpu(a.before,a.after)
