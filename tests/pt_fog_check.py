"""Production GLSL fog math, transport/source contracts and optional SPIR-V ABI."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import statistics
import tempfile

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'code/renderer_vulkan'

def check(cxx,dis):
    source=(R/'shaders/pt_fog_volume.glsl').read_text()
    body=source[source.index('bool fogClip('):].replace('.xyz','.xyz()')
    body=re.sub(r'(?:inout|out) (\w+) (\w+)',r'\1 &\2',body)
    with tempfile.TemporaryDirectory(prefix='pt-fog-') as tmp:
        p=Path(tmp);(p/'pt_fog_volume.inc').write_text(body)
        loader=(R/'pt_fog.h').read_text()
        (p/'pt_fog_bounds.inc').write_text(loader[loader.index('static qboolean fog_bound_plane'):loader.index('static void fog_load')])
        shader=(R/'shaders/pt_integrator.glsl').read_text()
        primary=shader[shader.index('bool primaryHit('):shader.index('#include "pt_fog_query.glsl"')]
        query=(R/'shaders/pt_fog_query.glsl').read_text()
        for name,code in (('primary',primary),('query',query)):
            (p/f'pt_fog_{name}.inc').write_text(re.sub(r'(?:inout|out) (\w+) (\w+)',r'\1 &\2',code))
        exe=p/'fog.exe'
        subprocess.run([cxx,'-O2',str(ROOT/'tests/pt_fog_fixture.cpp'),'-I',tmp,'-o',str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=20)
    native=(R/'vk_pathtrace.c').read_text();loader=(R/'pt_fog.h').read_text()
    for token in ('bindings[49]','VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 42','bind_buffer(48, pt.fog.buffer, pt.fog.size)',
                  'buffer_destroy(&pt.fog)','if (pt.fog_dirty)','buffer_upload(cmd, &pt.fog, pt.fog.size)'):
        assert token in native,token
    assert 'if (pt.fog_dirty)' in native and 'pt.fog_dirty = qfalse;' in native
    assert 'total*sizeof(vec4_t)' in loader and 'out->planes[1] = cursor-out->planes[0]' in loader
    assert 'if (fog_bound_plane(fog, planes+index)) continue;' in loader
    assert 'AddPointToBounds(fog->bounds[1], data->mins, data->maxs)' in loader
    assert 'MAX_MAP_BRUSHSIDES-n' in loader and 'plane_count' in loader
    shader=(R/'shaders/pt_integrator.glsl').read_text()
    assert 'fogTransmittance(origin,direction,0,distance)' in shader
    assert 'fogPathHit(origin,direction,segmentDistance,bounce,hit,fogDistance,fogVolume)' in shader
    assert 'if(!found && fogVolume!=0xffffffffu)' in shader
    query=(R/'shaders/pt_fog_query.glsl').read_text()
    assert query.index('fogSample(')<query.index('primaryHit(')
    assert 'float maximum=event ? fogDistance:pc.parameters.y;' in query
    assert query.count('fogSample(')==1, 'Never resample conditioned on the geometry result'
    assert 'addIncident(fogVolumes[fogVolume].emission.rgb)' in shader
    assert 'if(!fogScatter(fogVolume,fogWeight)) break;' in shader and 'attenuate(fogWeight)' in shader
    assert shader.index('addIncident(fogVolumes[fogVolume].emission.rgb)') < shader.index('if(!fogScatter(fogVolume,fogWeight))')
    assert 'addIncident(fogLighting(fogPoint,bounce+1>=int(pc.sampling.y)))' in shader
    assert 'previousPDF=1/(4*PI); segmentDistance=0;' in shader
    assert 'reactive=reactive || fogTransmittance' in shader
    light=(R/'shaders/pt_fog_lighting.glsl').read_text()
    for token in ('sampleEmitter()', 'pc.sunRadiance.rgb', 'pointAttenuation(', 'proposedLight(', 'visibility(world,direction,'):
        assert token in light,token
    assert 'dot(n,' not in light and 'dot(geometric,' not in light
    assert 'sampledEmitterGeometry(primitive,sampledEmitterIndex,root,bary' in light
    assert 'emitterPower*(distance*distance)/max(lightSelection.x*cosine,0.000001)' in light
    for name,default in [('r_pathTracingSamples','2'),('r_pathTracingAdaptive','0')]:
        assert f'Cvar_Get("{name}", "{default}",' in (R/'tr_cvar.c').read_text()
    print('PASS: native resource lifecycle, physical primary/bounce/shadow wiring, light families and unchanged flat-two defaults')
    if dis:
        for name in ('pt_rr_trace','pt_rr_guides','pathtrace','pt_staged_1'):
            asm=subprocess.check_output([dis,str(R/f'shaders/Compiled/{name}.cspv')],text=True,timeout=30)
            assert re.search(r'OpDecorate %\w+ Binding 48\b',asm),name
            blocks=re.findall(r'OpName (%\w+) "FogVolumes"',asm)
            assert blocks,name
            for block in blocks:
                for member,offset in enumerate((0,16,32,48,20528)):
                    assert re.search(r'OpMemberDecorate '+re.escape(block)+fr' {member} Offset {offset}\b',asm),name
            assert re.search(r'OpDecorate %\w*FogVolume\w* ArrayStride 80\b',asm),name
        print('PASS: compiled primary, RR guide, fallback and staged fog layout: binding 48, 80-byte records, planes at 20528')

def gpu_run(directory):
    name=directory.name.removeprefix('performance-')
    summary=json.loads((directory.parent/f'pt-perf-{name}.run.json').read_text(encoding='utf-8-sig'))
    manifest=json.loads((directory/'settings.json').read_text(encoding='utf-8-sig'))
    log=(directory/'baseq3/qconsole.log').read_text(errors='replace')
    assert summary['sourceSettingsUnchanged'] and summary['exitCode']==0 and not summary['timedOut']
    assert summary['completionMarker'] and summary['elapsedSeconds']<=45
    assert 'PT_PROGRAM_COMPLETE' in log
    for key,value in (('r_dlss','5'),('r_dlssFrameGeneration','0'),('r_dlssNeuralRendering','0'),
                      ('r_pathTracingSamples','2'),('r_pathTracingAdaptive','0'),('r_pathTracingBounces','4')):
        assert manifest['settings'][key]==value,(key,manifest['settings'][key])
    assert 'Ray Reconstruction: supported 1, enabled 1' in log
    assert 'Ray Reconstruction evaluation failed' not in log
    if summary['validationLayerLoaded']:
        assert not (directory/'validation.log').read_text().strip()
        assert '[debugUtilsMessengerCallback]' not in log
        assert not re.search(r'VUID-|SYNC-HAZARD-|Validation (?:Error|Warning)',log)
    counts={'q3tourney2':7,'q3ctf3':3,'q3dm6':0}
    assert f"Path tracing fog: {counts[manifest['map'].lower()]} convex volumes" in log
    captures={'q3tourney2':('fog_bridge','fog_inside'),'q3ctf3':('fog_outside','fog_inside'),'q3dm6':('fog_clear',)}
    expected_captures = ('fog_inside',) if manifest['config']=='pt_fog_inside.cfg' else captures[manifest['map'].lower()]
    for capture in expected_captures:
        assert (directory/f'baseq3/screenshots/{capture}.tga').stat().st_size>1920*1080*3
    rows=[dict((k,float(v)) for k,v in re.findall(r'(\w+)=([\d.]+)',line))
          for line in log.splitlines() if line.startswith('PT_PROFILE ')]
    if rows:
        assert len(rows)>=20
        print(name,{k:round(statistics.median(row[k] for row in rows),3) for k in ('frame','trace','guides')})
    print(f'PASS: {name}: map fog records, DLAA/RR, fixed two samples, FG/NR off, completed, saved settings untouched; validation={summary["validationLayerLoaded"]}')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx',required=True);p.add_argument('--spirv-dis')
    p.add_argument('--gpu-run',type=Path,action='append',default=[])
    a=p.parse_args();check(a.cxx,a.spirv_dis)
    for directory in a.gpu_run: gpu_run(directory)
