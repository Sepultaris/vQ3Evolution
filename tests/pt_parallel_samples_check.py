"""Concurrent four-sample bounds, output contract and production seed checks."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
SHADERS=ROOT/'code/renderer_vulkan/shaders'


def seed_fixture(compiler):
    sampling=(SHADERS/'pt_sampling.glsl').read_text()
    hash_function=sampling[sampling.index('uint sampleHash('):sampling.index('float blueNoiseSample(')]
    seed=(SHADERS/'pt_sample_seed.glsl').read_text()
    fixture='''#include <cstdint>
#include <cassert>
#include <cstdio>
using uint=uint32_t;
'''+hash_function+seed+'''
int main() {
    uint bins[4][256]={}; double sums[4]={};
    for(uint i=0;i<1000000u;++i) {
        uint pixel=i%2073600u,frame=i/4096u;
        uint previous[4]={};
        for(uint s=0;s<4u;++s) {
            uint r=independentPathSeed(pixel,frame,s);
            assert(r!=0u);
            if(s==0u) { uint old=(pixel+1u)*747796405u+(frame+1u)*2891336453u; assert(r==(old ? old:1u)); }
            for(uint p=0;p<s;++p) assert(r!=previous[p]);
            previous[s]=r;
            r^=r<<13; r^=r>>17; r^=r<<5;
            ++bins[s][r>>24]; sums[s]+=double(r>>8)/16777216.0;
        }
    }
    for(uint s=0;s<4u;++s) {
        assert(sums[s]>497000.0 && sums[s]<503000.0);
        for(uint b=0;b<256u;++b) assert(bins[s][b]>3400u && bins[s][b]<4450u);
    }
    std::puts("PASS: four million production seeds; nonzero, distinct sample streams, first-sample compatibility and uniformity smoke checks");
}
'''
    with tempfile.TemporaryDirectory(prefix='pt-parallel-seeds-') as folder:
        folder=Path(folder)
        source=folder/'seed.cpp'; source.write_text(fixture)
        exe=folder/'seed.exe'
        subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],check=True,timeout=30)
        subprocess.run([str(exe)],check=True,timeout=30)


class ParallelSamplesTests(unittest.TestCase):
    def test_every_tile_has_four_paths_one_writer_and_no_scratch_overlap(self):
        for width,height in ((1,1),(3,7),(4,4),(8,5),(63,65),(1920,1080),(1919,1079)):
            # Interior and edge groups exercise the same local mapping.
            groups={(0,0),((width-1)//4,(height-1)//4),((width-1)//4,0),(0,(height-1)//4)}
            for gx,gy in groups:
                slots=set(); paths={}; writers={}
                for lane in range(4):
                    for y in range(4):
                        for x in range(4):
                            local=y*4+x; slot=(lane*16+local)*4
                            for channel in range(4):
                                self.assertNotIn(slot+channel,slots)
                                self.assertLess(slot+channel,256)
                                slots.add(slot+channel)
                            pixel=(gx*4+x,gy*4+y)
                            if pixel[0]<width and pixel[1]<height:
                                paths[pixel]=paths.get(pixel,0)+1
                                writers[pixel]=writers.get(pixel,0)+(lane==0)
                self.assertEqual(len(slots),256)
                self.assertTrue(all(count==4 for count in paths.values()))
                self.assertTrue(all(count==1 for count in writers.values()))

    def test_all_lanes_initialize_before_unconditional_barrier(self):
        shader=(SHADERS/'pt_parallel_samples.glsl').read_text()
        before,after=shader.split('    barrier();',1)
        self.assertNotIn('return;',before)
        self.assertIn('if(!valid || lane!=0u) return;',after)
        for channel in ('','+1u','+2u','+3u'):
            self.assertIn('ptSampleRadiance[slot'+channel+']=vec4(',before)
        self.assertEqual(before.count('integrator('),1)
        for c in 'DSTE':
            self.assertIn('!any(isnan(radiance'+c+')) && !any(isinf(radiance'+c+'))',before)
        self.assertIn('sampleIndex<4u;++sampleIndex',after)
        self.assertIn('uint source=(sampleIndex*16u+localPixel)*4u;',after)

    def test_history_channel_and_tone_mapping_contract_is_identical(self):
        original=(SHADERS/'pt_integrator.glsl').read_text()
        parallel=(SHADERS/'pt_parallel_samples.glsl').read_text()
        start='    diffuse/=pc.sampling.x;'
        end='    if(rp.options.z<4) imageStore(outputColor,pixel,vec4(toneMap(result),1));'
        def output(s): return s[s.index(start):s.index(end)+len(end)]
        self.assertEqual(output(original),output(parallel))

    def test_rejected_experiment_is_not_in_the_runtime_build(self):
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertNotIn('r_pathTracingParallelSamples',native)
        self.assertNotIn('pt_parallel_comp_spv',native)
        self.assertNotIn('pt_parallel_comp.o',(ROOT/'Makefile').read_text())
        wrapper=(ROOT/'tests/pt_parallel_samples.comp').read_text()
        self.assertIn('#define PT_PARALLEL_SAMPLES 1',wrapper)
        source=(SHADERS/'pt_parallel_samples.glsl').read_text()
        self.assertNotIn('layout(binding=',source)
        self.assertIn('shared vec4 ptSampleRadiance[256]',source)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx',required=True)
    args=parser.parse_args(); seed_fixture(args.cxx)
    unittest.main(argv=[__file__])
