"""Stock hologram asset/wiring checks and optional matched real GPU captures."""
import argparse
import json
from pathlib import Path
import re
import struct
import zipfile

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'code/renderer_vulkan'

def asset(pak):
    with zipfile.ZipFile(pak) as z:
        data=z.read('models/mapobjects/bitch/fembotbig.md3')
        header=struct.unpack_from('<4si64s9i',data)
        assert header[0]==b'IDP3' and header[6]==1
        surface=header[-2];fields=struct.unpack_from('<4s64s10i',data,surface)
        shader=struct.unpack_from('<64si',data,surface+fields[-4])[0].split(b'\0')[0].decode()
        assert shader=='models/mapobjects/bitch/hologirl.tga'
        source=re.sub(r'//[^\r\n]*','',z.read('scripts/models.shader').decode('latin1'))
        tokens=re.findall(r'[^\s{}]+|[{}]',source);start=tokens.index(shader[:-4]);end=start+1;depth=0
        while end<len(tokens):
            if tokens[end]=='{':depth+=1
            if tokens[end]=='}':
                depth-=1
                if depth==0:break
            end+=1
        text=' '.join(tokens[start:end+1]).lower()
        for token in ('deformvertexes move 0 0 .7 sin 0 5 0 0.2','alphafunc ge128 depthwrite',
                      'map models/mapobjects/bitch/hologirl2.tga tcgen environment',
                      'tcmod scroll -6 -.2 tcmod scale 1 1 blendfunc gl_one gl_one'):
            assert token in text,token
        assert 'depthfunc equal' not in text
        bsp=z.read('maps/q3dm0.bsp');offset,size=struct.unpack_from('<2i',bsp,8)
        entities=bsp[offset:offset+size].decode('latin1')
        entity=next(e for e in re.findall(r'\{[^}]*\}',entities) if 'fembotbig.md3' in e)
        assert 'func_rotating' in entity and '-1465 -1596 22' in entity
    print('PASS: installed stock Q3DM0 rotating model uses the cutout body and unrestricted scrolling environment-glow shader covered by the fixture')

def source():
    native=(R/'vk_pathtrace.c').read_text();shader=(R/'shaders/pt_integrator.glsl').read_text()
    assert '(additive && has_surface_base && !shader->numDeforms)' in native
    assert 'blend | (candidate->stateBits & GLS_DEPTHFUNC_EQUAL)' in native
    assert 'if (independent_glow && m->params[0] == 0 && m->surface[3] != 0 && m->composition[3] == 0)' in native
    assert 'm->maps[3] = 3;' in native
    assert 'hologirl' not in native, 'Shader semantics, not a hardcoded map/model exception'
    assert 'bool independentGlow=purpose!=2u && materials[materialAndFlags&0xffffu].maps.w==3;' in shader
    assert '(independentGlow || passesAlpha(primitive,bary,origin))' in shader
    helper=(R/'shaders/pt_glow_coverage.glsl').read_text()
    assert 'if(m.maps.w==3 && !passesAlpha(primitive,bary,observer)) m.params.x=2;' in helper
    assert 'surfaceHitMaterial(hit.primitive,hit.bary,origin)' in shader
    assert 'surfaceHitMaterial(hit.primitive,hit.bary,pc.originNear.xyz)' in shader
    visibility=shader.split('vec3 visibility(',1)[1].split('vec3 basisSample',1)[0]
    assert 'if(!passesAlpha(primitive,bary,origin)) continue;' in visibility
    assert 'independentGlow' not in visibility
    assert 'if(rejectedBase && m.maps.w!=3) return vec3(0);' in shader
    assert 'if(rejectedBase && (uint(generators.w)&0x20000u)!=0u) continue;' in shader
    print('PASS: retained glow, distinct body occlusion, transparent continuation and native/RR guides, authored depth-equal masking; no model-name exception')

def gpu(before,after):
    import numpy as np
    from PIL import Image
    manifests=[];images=[]
    for directory in (before,after):
        label=directory.name.removeprefix('performance-')
        summary=json.loads((directory.parent/f'pt-perf-{label}.run.json').read_text(encoding='utf-8-sig'))
        manifest=json.loads((directory/'settings.json').read_text(encoding='utf-8-sig'));manifests.append(manifest)
        log=(directory/'baseq3/qconsole.log').read_text(errors='replace')
        assert summary['exitCode']==0 and not summary['timedOut'] and summary['completionMarker']
        assert summary['elapsedSeconds']<=45 and summary['sourceSettingsUnchanged']
        assert manifest['map']=='q3dm0' and manifest['config']=='pt_hologram.cfg'
        for key,value in [('r_dlss','5'),('r_pathTracingSamples','2'),('r_pathTracingAdaptive','0'),
                          ('r_dlssFrameGeneration','0'),('r_dlssNeuralRendering','0')]:
            assert manifest['settings'][key]==value
        assert 'Ray Reconstruction: supported 1, enabled 1' in log
        assert 'Ray Reconstruction evaluation failed' not in log
        assert not re.search(r'Unknown command|VUID-|SYNC-HAZARD|debugUtilsMessengerCallback',log,re.I)
        for name in ('front','animated'):
            path=directory/f'baseq3/screenshots/hologram_{name}.tga'
            im=Image.open(path);im.save(path.with_suffix('.png'))
            if name=='front':images.append(np.asarray(im,dtype=float)[330:670,875:1030,:3])
        if directory==after:
            assert summary['validationLayerLoaded'] and not (directory/'validation.log').read_text().strip()
    assert manifests[0]['settings']==manifests[1]['settings']
    assert manifests[0]['sourceSha256']==manifests[1]['sourceSha256']
    assert manifests[0]['rendererSha256']!=manifests[1]['rendererSha256']
    # The narrow scrolling bands occupy little of the model/background ROI.
    # Whole-region mean alone is a poor glow test: require restored bright
    # texels, not uniform brightening of the room or merely a changed pose.
    restored=int(np.count_nonzero((images[1].mean(2)>120)&((images[1]-images[0]).mean(2)>40)))
    assert restored>128,restored
    means=[float(im.mean()) for im in images]
    print(f'PASS: saved-quality before/after, clean Vulkan, unchanged settings; {restored} restored bright model-region pixels, RGB mean {means[0]:.2f} -> {means[1]:.2f}/255 (visual inspection still required)')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--pak',type=Path);p.add_argument('--before',type=Path);p.add_argument('--after',type=Path)
    a=p.parse_args();source()
    if a.pak:asset(a.pak)
    if a.before or a.after:
        assert a.before and a.after
        gpu(a.before,a.after)
