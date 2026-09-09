"""Check RR workgroup selection, partial-edge coverage, and matched captures."""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc')
p.add_argument('--runs',nargs='+',type=Path)
a=p.parse_args()
if a.cc:
    header=(ROOT/'code/renderer_vulkan/pt_rr_workgroup.h').as_posix()
    fixture='''#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
'''+f'#include "{header}"\n'+'''
int main(void) {
    assert(rr_workgroup_rows(0x10de,8,128,1024,0)==128);
    assert(rr_workgroup_rows(0x1002,8,128,1024,0)==8);
    assert(rr_workgroup_rows(0x10de,8,64,512,128)==64);
    assert(rr_workgroup_rows(0x10de,4,128,1024,32)==0);
    assert(rr_workgroup_rows(0x10de,8,128,32,0)==0);
    assert(rr_workgroup_rows(0x10de,8,128,1024,17)==128);
    unsigned sizes[][2]={{1,1},{17,13},{1919,1079},{1920,1080}};
    unsigned choices[]={8,32,64,128};
    for(unsigned c=0;c<4;++c) for(unsigned s=0;s<4;++s) {
        unsigned rows=rr_workgroup_rows(0x10de,8,128,1024,choices[c]);
        assert(rows==choices[c]);
        unsigned w=sizes[s][0],h=sizes[s][1];
        unsigned char *coverage=calloc(w*h,1); assert(coverage);
        for(unsigned gy=0;gy<(h+rows-1)/rows;++gy) for(unsigned gx=0;gx<(w+7)/8;++gx)
            for(unsigned ly=0;ly<rows;++ly) for(unsigned lx=0;lx<8;++lx) {
                unsigned x=gx*8+lx,y=gy*rows+ly;
                if(x<w && y<h) ++coverage[y*w+x];
            }
        for(unsigned i=0;i<w*h;++i) assert(coverage[i]==1);
        free(coverage);
    }
    puts("PASS: workgroup limits/fallback and exact pixel coverage for all four sizes");
}
'''
    with tempfile.TemporaryDirectory(prefix='rr-workgroup-') as folder:
        folder=Path(folder); (folder/'test.c').write_text(fixture)
        subprocess.run([a.cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(folder/'test.c'),'-o',str(folder/'test.exe')],check=True,timeout=30)
        subprocess.run([str(folder/'test.exe')],check=True,timeout=10)
if a.runs:
    from PIL import Image, ImageChops, ImageStat
    reference=None; reference_image=None; script=None; camera=None
    for run in a.runs:
        manifest=json.loads((run/'settings.json').read_text(encoding='utf-8-sig'))
        summary=json.loads((run.parent/('pt-perf-'+run.name.removeprefix('performance-')+'.run.json')).read_text(encoding='utf-8-sig'))
        log=(run/'baseq3/qconsole.log').read_text(errors='replace')
        settings=manifest['settings'].copy(); requested=settings.pop('r_pathTracingRRRows')
        assert settings['r_dlss']=='5' and settings['r_pathTracingSamples']=='2' and settings['r_pathTracingAdaptive']=='0'
        assert settings['r_dlssFrameGeneration']=='0' and settings['r_dlssRayReconstruction']=='1'
        current=(settings,manifest['sourceSha256'],manifest['sharedSourceSha256'],manifest['rendererSha256'],manifest['executableSha256'])
        if reference is None: reference=current
        assert current==reference,'Benchmark inputs changed'
        assert summary['sourceSettingsUnchanged'] and summary['sharedSourceSettingsUnchanged']
        assert f'PT_RR_WORKGROUP rows={requested} requested={requested}' in log
        assert 'PT_PROGRAM_COMPLETE' in log and 'VK_ERROR_' not in log
        views=re.findall(r'^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)$',log,re.M)
        assert len(views)==2 and views[0]==views[1],'Camera moved'
        if camera is None: camera=views[0]
        assert views[0]==camera,'Camera differs between runs'
        scenario=(run/'baseq3/pt_rr_workgroup.cfg').read_bytes()
        if script is None: script=scenario
        assert scenario==script,'Scenario changed'
        rows=[{k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)',line)} for line in log.splitlines() if line.startswith('PT_PROFILE ')][3:-3]
        assert len(rows)>=20
        image=Image.open(run/'baseq3/screenshots/rr_workgroup_stationary.tga').convert('RGB')
        if reference_image is None: reference_image=image
        mae=ImageStat.Stat(ImageChops.difference(reference_image,image).crop((0,90,image.width,image.height-120))).mean
        clean=summary['exitCode']==0 and not summary['timedOut']
        print(json.dumps(dict(run=run.name,rows=requested,accepted=clean,samples=len(rows),ms={k:statistics.median(r[k] for r in rows) for k in rows[0]},world_rgb_mae=mae)))
        if not clean: print('DIAGNOSTIC ONLY: process did not exit cleanly.')
