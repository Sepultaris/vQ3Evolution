"""Compile-only scene-record references: exact medium values and decal ordering."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SHADERS=ROOT/'code/renderer_vulkan/shaders'

def run(compiler):
    decals=(SHADERS/'pt_decals.glsl').read_text()
    body=decals[decals.index('        uint polygon='):decals.index('    // Map the receiver')].rsplit('\n    }',1)[0]
    body=body.replace('rayQueryGetIntersectionBarycentricsEXT(query,false)','candidate.bary')
    macro=decals[decals.index('#ifdef PT_COMPACT_DECALS'):decals.index('\nbool decalReceiver')]
    medium=(SHADERS/'pt_medium_records.glsl').read_text()
    medium=medium.replace('rp.cameraMedium.yzw','vec3(rp.cameraMedium.y,rp.cameraMedium.z,rp.cameraMedium.w)')
    medium=medium.replace('materials[id].absorption.rgb','vec3(materials[id].absorption.x,materials[id].absorption.y,materials[id].absorption.z)')
    with tempfile.TemporaryDirectory(prefix='pt-scene-records-') as folder:
        folder=Path(folder)
        (folder/'pt_medium_records.inc').write_text(medium)
        for name,on in [('reference',False),('compact',True)]:
            header=('#define PT_COMPACT_DECALS 1\n' if on else '#undef PT_COMPACT_DECALS\n')+'#undef DECAL_POLYGON\n'+macro
            header+='\nstatic Collection '+name+'(const std::vector<Candidate> &hits) {\n'
            header+='    const int MAX_HIT_DECALS=16;uint primitives[16];vec2 barycentrics[16];\n'
            if not on:header+='    uint polygons[16];\n'
            header+='    int count=0;bool touched=false;\n    for(const auto &candidate:hits) {uint primitive=candidate.primitive;\n'+body+'\n    }\n'
            header+='    Collection result={};result.count=count;result.touched=touched;for(int i=0;i<count;++i){result.primitives[i]=primitives[i];result.barycentrics[i]=barycentrics[i];}return result;\n}\n'
            (folder/('pt_decal_'+('records' if on else 'reference')+'.inc')).write_text(header)
        for flags in (['-O2'],['-O3','-ffast-math']):
            exe=folder/'records.exe'
            subprocess.run([compiler,'-std=c++17',*flags,'-Wall','-Wextra','-Werror',str(ROOT/'tests/pt_compact_records_fixture.cpp'),'-I',str(folder),'-o',str(exe)],check=True,timeout=30)
            subprocess.run([str(exe)],check=True,timeout=30)

class RecordTests(unittest.TestCase):
    def test_rejected_experiments_are_compile_only(self):
        for wrapper in SHADERS.glob('*.comp'):
            self.assertNotIn('#define PT_COMPACT_MEDIA',wrapper.read_text())
            self.assertNotIn('#define PT_COMPACT_DECALS',wrapper.read_text())
        self.assertNotIn('r_pathTracingCompactRecords',(ROOT/'code/renderer_vulkan/tr_cvar.c').read_text())
    def test_stack_sites_and_capacity_preserved(self):
        source=(SHADERS/'pt_integrator.glsl').read_text()
        self.assertIn('uint mediumRecords[4]',source)
        self.assertIn('mediumRecords[0]=0xffffffffu; mediumCount=1;',source)
        self.assertIn('mediumRecords[0]=triangleMaterials[hit.primitive]&0xffffu; mediumCount=1;',source)
        self.assertIn('mediumRecords[mediumCount++]=triangleMaterials[hit.primitive]&0xffffu;',source)
        self.assertIn('if(mediumCount>=4) break;',source)
        self.assertEqual(source.count('MEDIUM_ABSORPTION(mediumCount-1)'),3)
        self.assertIn('#define MEDIUM_IOR(index) mediumIOR[index]',source)
        self.assertIn('#define MEDIUM_ABSORPTION(index) mediumAbsorption[index]',source)
    def test_scene_properties_are_immutable_during_dispatch(self):
        cache=(SHADERS/'pt_material_cache.comp').read_text()
        for write in ('.optical=', '.absorption='):
            self.assertNotIn(write,cache)

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cxx',required=True);a=p.parse_args()
    run(a.cxx);unittest.main(argv=[__file__])
