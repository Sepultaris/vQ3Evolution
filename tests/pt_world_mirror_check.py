"""Compile the production mirror classifier and parser; optionally use installed BSP entities."""
import argparse
import json
from pathlib import Path
import struct
import re
import subprocess
import tempfile
import zipfile

ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'code/renderer_vulkan'

def run(cc,sdk,pak):
    with tempfile.TemporaryDirectory(prefix='pt-mirror-') as tmp:
        exe=Path(tmp)/'mirror.exe'
        subprocess.run([cc,'-O2','-ffunction-sections','-fdata-sections','-Wl,--gc-sections',
            str(ROOT/'tests/pt_world_mirror_fixture.c'),str(R/'R_Parser.c'),str(ROOT/'code/qcommon/q_math.c'),
            '-I',str(R),'-I',str(sdk/'Include'),'-o',str(exe)],check=True,timeout=30)
        args=[str(exe)]
        if pak:
            with zipfile.ZipFile(pak) as archive: bsp=archive.read('maps/q3dm0.bsp')
            start,length=struct.unpack_from('<2i',bsp,8)
            entities=Path(tmp)/'entities.txt'; entities.write_bytes(bsp[start:start+length]); args.append(str(entities))
        subprocess.run(args,check=True,timeout=10)
    native=(R/'vk_raytracing.c').read_text()
    assert 'surface->shader->sort == SS_PORTAL && !world_surface_is_mirror(surface)' in native
    assert native.count('world_surface_counts(')==3
    print('PASS: both geometry sizing and upload use entity-confirmed mirror inclusion')

def gpu(before,after):
    manifests=[]
    for directory in (before,after):
        label=directory.name.removeprefix('performance-')
        summary=json.loads((directory.parent/f'pt-perf-{label}.run.json').read_text(encoding='utf-8-sig'))
        manifest=json.loads((directory/'settings.json').read_text(encoding='utf-8-sig')); manifests.append(manifest)
        log=(directory/'baseq3/qconsole.log').read_text(errors='replace')
        assert summary['completionMarker'] and not summary['timedOut'] and summary['exitCode']==0
        assert summary['sourceSettingsUnchanged'] and summary['elapsedSeconds']<=45
        assert manifest['map']=='q3dm0' and manifest['config']=='pt_stock_mirror.cfg'
        for key,value in (('r_dlss','5'),('r_dlssFrameGeneration','0'),('r_dlssNeuralRendering','0'),
                          ('r_pathTracingSamples','2'),('r_pathTracingAdaptive','0')):
            assert manifest['settings'][key]==value,(key,value)
        assert 'Ray Reconstruction: supported 1, enabled 1' in log
        assert 'Ray Reconstruction evaluation failed' not in log
        for name in ('front','angle'):
            assert (directory/f'baseq3/screenshots/stock_mirror_{name}.tga').stat().st_size>1920*1080*3
        if directory==after:
            assert 'Path tracing: 1 static BSP mirror surfaces loaded' in log
            assert summary['validationLayerLoaded']
            assert not (directory/'validation.log').read_text().strip()
            assert not re.search(r'VUID-|SYNC-HAZARD|debugUtilsMessengerCallback',log)
    assert manifests[0]['settings']==manifests[1]['settings']
    assert manifests[0]['sourceSha256']==manifests[1]['sourceSha256']
    assert manifests[0]['rendererSha256']!=manifests[1]['rendererSha256']
    print('PASS: matched DLAA/RR/two-sample runs completed, mirror loaded, two captures, clean validation and saved configuration unchanged; inspect captures for appearance')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',required=True)
    p.add_argument('--sdk',type=Path,required=True);p.add_argument('--pak',type=Path)
    p.add_argument('--gpu-before',type=Path);p.add_argument('--gpu-after',type=Path)
    a=p.parse_args();run(a.cc,a.sdk,a.pak)
    if a.gpu_before or a.gpu_after:
        assert a.gpu_before and a.gpu_after
        gpu(a.gpu_before,a.gpu_after)
