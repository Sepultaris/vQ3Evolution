"""Production material-cache arithmetic and native/cache lifetime contracts."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]; SHADERS=ROOT/'code/renderer_vulkan/shaders'

def cpp(s):
    s=re.sub(r'(cached\.[ab])\.(xy|zw)=value;',lambda m:'{ '+' '.join(m[1]+'.'+c+'=value.'+v+';' for c,v in zip(m[2],'xy'))+' }',s)
    for swizzle,ctor in [('yzw','vec3'),('xyz','vec3'),('yz','vec2'),('xy','vec2'),('zw','vec2')]:
        s=re.sub(r'(\w+\.\w+)\.'+swizzle+r'\b',lambda m:ctor+'('+','.join(m[1]+'.'+c for c in swizzle)+')',s)
    return re.sub(r'(?<![\w.])(\d+\.\d*(?:[eE][+-]?\d+)?)(?![\w.])',r'\1f',s)

def run(compiler):
    source=(SHADERS/'pt_integrator.glsl').read_text()
    consumer=source.split('TexMod cached=materials[id].layers[layerIndex].mods[3];',1)[1].split('        return sampleTexture(cached.b.z',1)[0]
    consumer=consumer.replace('int(materials[id].layers[layerIndex].params.w)','count').replace('materials[id].layers[layerIndex].mods[i]','mods[i]')
    original=(ROOT/'tests/pt_material_cache_reference.glsl').read_text()
    original=original.replace('materials[id].layers[layerIndex].mods[i]','mods[i]')
    compact=(SHADERS/'pt_material_cache.comp').read_text()
    compact=compact[compact.index('    for(int i=0;'):compact.index('    int frame=')]
    compact=compact.replace('int(materials[id].layers[layer].params.w)','count').replace('materials[id].layers[layer].mods[i]','mods[i]')
    with tempfile.TemporaryDirectory(prefix='pt-material-cache-') as folder:
        folder=Path(folder)
        for name,s in [('pt_wave.inc',(SHADERS/'pt_wave.glsl').read_text()),('pt_cache_original.inc',original),('pt_cache_compact.inc',compact),('pt_cache_consumer.inc',consumer)]:
            (folder/name).write_text(cpp(s))
        for flags in (['-O2'],['-O3','-ffast-math']):
            exe=folder/'material-cache.exe'
            subprocess.run([compiler,'-std=c++17',*flags,'-Wall','-Wextra','-Werror',str(ROOT/'tests/pt_material_cache_fixture.cpp'),'-I',str(folder),'-o',str(exe)],check=True,timeout=30)
            subprocess.run([str(exe)],check=True,timeout=30)

class MaterialCacheTests(unittest.TestCase):
    def test_only_unused_storage_and_independent_inputs(self):
        shader=(SHADERS/'pt_integrator.glsl').read_text()
        fast=shader[shader.index('    // Cache only native-classified'):shader.index('    Layer layer=layerProperties')]
        for guard in ('vectors[0].w==2','!context.uniformTint','all(equal(context.timeScroll,vec3(0)))','params.w<4','mods[3].b.w==1'):
            self.assertIn(guard,fast)
        cache=(SHADERS/'pt_material_cache.comp').read_text()
        self.assertIn('vectors[0].w!=2 || materials[id].layers[layer].params.w>=4',cache)
        writes=re.findall(r'^\s*materials\[id\]\.layers\[layer\]\.([\w.\[\]]+)=(?!=)',cache,re.M)
        self.assertEqual(writes,['mods[3].a','mods[3].b','mods[3].wave'])
        self.assertIn('if(id>=uint(pc.sampling.x) || layer>=int(materials[id].composition.x)) return;',cache)

    def test_layout_definitions_match_producer_and_consumer(self):
        original=(SHADERS/'pt_integrator.glsl').read_text();cache=(SHADERS/'pt_material_cache.comp').read_text()
        for name in ('TexMod','LayerRecord','MaterialRecord'):
            pattern=r'struct '+name+r' \{.*?\};'
            self.assertEqual(re.search(pattern,original,re.S)[0],re.search(pattern,cache,re.S)[0])

    def test_reference_shader_contains_no_cache_program(self):
        original=(SHADERS/'pt_integrator.glsl').read_text()
        self.assertIn('#ifdef PT_MATERIAL_CACHE',original)
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('(r_pathTracingMaterialCache->integer && lighting_material_cache_eligible() ? 32u : 0u)',native)
        self.assertIn('(pt.lighting_mode & 32) && material_cache_initialize()',native)
        self.assertNotIn('r_pathTracingParallelSamples',native)
        for suffix in ('','_brdf','_loop','_loop_brdf'):
            name='pt_cached_materials'+suffix
            self.assertIn('#define PT_MATERIAL_CACHE 1',(SHADERS/(name+'.comp')).read_text())
            self.assertIn(name+'_comp.o',(ROOT/'Makefile').read_text())

    def test_cache_precedes_guides_and_restores_sample_count(self):
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        dispatch=native[native.index('    if (material_cache_active) {'):native.index('    uint32_t lighting_mode = pt.lighting_mode;')]
        self.assertLess(dispatch.index('c[28] = (float)pt.material_count'),dispatch.index('qvkCmdDispatch'))
        self.assertLess(dispatch.index('c[28] = (float)r_pathTracingSamples->integer'),dispatch.index('pt.guide_pipeline'))
        self.assertLess(dispatch.index('VK_ACCESS_SHADER_WRITE_BIT'),dispatch.index('pt.guide_pipeline'))
        self.assertIn('VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier',dispatch)
        self.assertIn('if (material_cache_active != pt.material_cache_active)',native)
        transition=native.split('if (material_cache_active != pt.material_cache_active)',1)[1].split('if (pt.materials_dirty',1)[0]
        self.assertIn('pt.materials_dirty = qtrue',transition)

    def test_cache_pipeline_failure_and_shutdown_are_safe(self):
        source=(ROOT/'code/renderer_vulkan/pt_material_cache.h').read_text()
        self.assertIn('if (pt.material_cache_failed) return qfalse;',source)
        self.assertIn('qvkDestroyShaderModule(vk.device, module, NULL)',source)
        self.assertIn('pt.material_cache_pipeline = VK_NULL_HANDLE',source)
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text().split('void vk_pt_shutdown(void)',1)[1]
        self.assertLess(native.index('if (pt.material_cache_pipeline)'),native.index('qvkDestroyPipelineLayout'))

    def test_shader_time_animation_and_tint_use_original_formulas(self):
        cache=(SHADERS/'pt_material_cache.comp').read_text()
        self.assertIn('#include "pt_wave.glsl"',cache)
        self.assertIn('rp.depthProjection.z-materials[id].layers[layer].meta.w',cache)
        self.assertIn('time=min(time,materials[id].layers[layer].meta.z)',cache)
        for field in ('rgbWave','alphaWave','generators.x','generators.y','color.rgb','color.a','params.x','params.y'):
            self.assertIn('materials[id].layers[layer].'+field,cache)

    def test_defaults_and_benchmark_roles(self):
        self.assertIn('Cvar_Get("r_pathTracingMaterialCache", "1", CVAR_CHEAT)',(ROOT/'code/renderer_vulkan/tr_cvar.c').read_text())
        active=None;seen=[]
        for line in (ROOT/'tests/pt_material_cache.cfg').read_text().splitlines():
            if line.startswith('set r_pathTracingMaterialCache '): active=int(line.split()[-1])
            if line.startswith('echo PT_PROGRAM_') and not line.endswith('COMPLETE'):
                phase=line.removeprefix('echo PT_PROGRAM_');self.assertEqual(active,int(phase.startswith('OPTIMIZED')));seen.append(phase)
        self.assertEqual(seen,['REFERENCE_A','OPTIMIZED_A','OPTIMIZED_B','REFERENCE_B'])

    def test_reverse_order_and_packed_geometry_comparison(self):
        for filename,cvar,expected in (
            ('pt_material_cache_confirm.cfg','r_pathTracingMaterialCache',['OPTIMIZED_A','REFERENCE_A','REFERENCE_B','OPTIMIZED_B']),
            ('pt_cached_emitters.cfg','r_pathTracingEmitterGeometry',['REFERENCE_A','OPTIMIZED_A','OPTIMIZED_B','REFERENCE_B'])):
            active=None;seen=[]
            config=(ROOT/'tests'/filename).read_text()
            if filename=='pt_cached_emitters.cfg': self.assertIn('set r_pathTracingMaterialCache 1',config)
            for line in config.splitlines():
                if line.startswith('set '+cvar+' '): active=int(line.split()[-1])
                if line.startswith('echo PT_PROGRAM_') and not line.endswith('COMPLETE'):
                    phase=line.removeprefix('echo PT_PROGRAM_');self.assertEqual(active,int(phase.startswith('OPTIMIZED')));seen.append(phase)
            self.assertEqual(seen,expected)

    def test_validation_output_is_isolated_per_run(self):
        runner=(ROOT/'tests/run-pt-performance.ps1').read_text()
        self.assertIn("Join-Path $ptHome 'validation.log'",runner)
        self.assertIn('$env:VK_LAYER_SETTINGS_PATH=$ptHome',runner)
        self.assertIn('khronos_validation.validate_sync = true',runner)
        self.assertNotIn('$env:VK_LAYER_SETTINGS_PATH=$ptProfile',runner)

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--cxx',required=True)
    args=parser.parse_args();run(args.cxx);unittest.main(argv=[__file__])
