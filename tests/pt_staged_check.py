"""Check extracted production stage formulas, state ABI and paired GPU output."""
import argparse
from pathlib import Path
import re
import statistics
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / 'code/renderer_vulkan/shaders'

def hardware_source():
    # The staged kernels are hardware-only; software profiling/NRD blocks in
    # the shared integrator do not participate in their compiled program.
    source=(SHADERS/'pt_integrator.glsl').read_text()
    return re.sub(r'#ifdef PT_SOFTWARE_(?:NRD|PROFILE)\n.*?#endif\n', '', source, flags=re.S)

def expected_surface(source):
    body = source[source.index('        PT_CATEGORY(0u);', source.index('for(int bounce=0;')):
                  source.index('        // Next-event estimation', source.index('void integrator'))]
    for before, after in (
        ('        vec3 n=shadingNormal(hit,direction,geometric), v=-direction;', '        n=shadingNormal(hit,direction,geometric);'),
        ('        vec3 albedo=baseColor', '        albedo=baseColor'),
        ('        float roughness=properties.x, metallic=properties.y;', '        roughness=properties.x; metallic=properties.y;'),
        ('        vec3 start=world+geometric*pc.parameters.x;', '        start=world+geometric*pc.parameters.x;'),
        ('vec3 world=origin+direction*hit.distance, geometric;', 'vec3 world=origin+direction*hit.distance;'),
        ('            previousPDF=-1; segmentDistance=0;\n            continue;', '            previousPDF=-1; segmentDistance=0;\n            return 1u;'),
        ('            previousPDF=1/(4*PI); segmentDistance=0;\n            continue;', '            previousPDF=1/(4*PI); segmentDistance=0;\n            return 1u;'),
        ('            --bounce;\n', ''), ('break;', 'return 0u;')):
        body = body.replace(before, after)
    return 'uint stagedSurface() {\n    for(;;) {\n' + body + '        return 2u;\n    }\n}\n'

def expected_lighting(source):
    start = source.index('        // Direct illumination/emission above')
    body = source[start:source.index('    PT_CATEGORY(0u);\n}', start)]
    body = body[:body.rindex('    }\n')].replace('break;', 'return 0u;')
    return ('uint stagedLighting() {\n    vec3 v=-direction;\n'
            '    prepareHitBRDF(n,v,albedo,roughness,metallic);\n'
            '#include "pt_light_loop.glsl"\n' + body + '    return 1u;\n}\n')

class StagedTests(unittest.TestCase):
    def test_surface_preserves_actual_material_and_optical_program(self):
        actual = (SHADERS/'pt_staged_surface.glsl').read_text()
        self.assertEqual(actual[actual.index('uint stagedSurface'):], expected_surface(hardware_source()))

    def test_lighting_and_continuation_preserve_actual_program(self):
        actual = (SHADERS/'pt_staged_lighting.glsl').read_text()
        self.assertEqual(actual[actual.index('uint stagedLighting'):], expected_lighting(hardware_source()))

    def test_rng_survives_sample_and_dispatch_boundaries(self):
        code = (SHADERS/'pt_staged.glsl').read_text()
        begin = code.split('void stagedBeginSample(',1)[1].split('#if PT_STAGED_PASS',1)[0]
        self.assertNotIn('rng=', begin)
        self.assertIn('uvec4(1u,rng,sampleDimension,ordinal)', begin)
        self.assertIn('rng=paths[pathIndex].control.y;', code)
        self.assertIn('sampleDimension=paths[pathIndex].control.z;', code)
        self.assertIn('paths[pathIndex].control.yz=uvec2(rng,sampleDimension);', code)
        self.assertEqual(code.count('747796405u'), 1)

    def test_footprint_and_all_radiance_channels_survive(self):
        code = (SHADERS/'pt_staged.glsl').read_text()
        for token in ('footprintPrimitive=floatBitsToUint(paths[pathIndex].misc.z)',
                      'footprintBarycentrics=mat2(paths[pathIndex].footprint.xy,paths[pathIndex].footprint.zw)',
                      'specular[samplePixel]=vec4(reflection,history+1)',
                      'transmission[samplePixel]=vec4(transmitted,history+1)',
                      'visibleEmission[samplePixel]=vec4(emission,history+1)'):
            self.assertIn(token, code)
        self.assertIn('for(int i=0;i<mediumCount;++i)', code)
        self.assertNotRegex(code, r'StagedState\s+\w+\s*=')

    def test_prototype_disabled_and_failure_does_not_lower_quality(self):
        cvars=(ROOT/'code/renderer_vulkan/tr_cvar.c').read_text()
        self.assertIn('Cvar_Get("r_pathTracingStaged", "0", CVAR_CHEAT)', cvars)
        native=(ROOT/'code/renderer_vulkan/pt_staged.h').read_text()
        for forbidden in ('Cvar_Set', 'r_pathTracingSamples->', 'r_pathTracingBounces->'):
            self.assertNotIn(forbidden, native)
        self.assertIn('bytes > properties.limits.maxStorageBufferRange', native)
        self.assertIn('fail:\n    staged_shutdown();\n    pt.staged.failed = qtrue;', native)
        self.assertIn('for (int sample = 0; sample < (int)c[28]; ++sample)', native)
        self.assertIn('for (int bounce = 0; bounce < (int)c[29]; ++bounce)', native)

    def test_separate_compatible_descriptor_layout_and_barriers(self):
        native=(ROOT/'code/renderer_vulkan/pt_staged.h').read_text()
        self.assertIn('sets[2] = { pt.set_layout, pt.staged.set_layout }', native)
        self.assertIn('VK_SHADER_STAGE_COMPUTE_BIT, 0, 128', native)
        self.assertIn('VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT', native)
        self.assertIn('VK_ACCESS_HOST_READ_BIT', native)
        self.assertIn('pt.layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(c), constants', native)

    def test_bands_keep_global_rng_and_output_addresses(self):
        code=(SHADERS/'pt_staged.glsl').read_text()
        self.assertIn('rng=(samplePixel+1u)*747796405u', code)
        self.assertIn('samplePixel=uint(pixel.y*size.x+pixel.x)', code)
        self.assertNotRegex(code, r'(accumulated|specular|transmission|visibleEmission)\[pathIndex\]')
        for width,height,rows in ((1920,1080,32),(1919,1081,8),(3440,1440,64),(7,3,3)):
            written=set()
            for first in range(0,height,rows):
                actual=min(rows,height-first)
                for y in range((actual+7)//8*8):
                    for x in range(width):
                        if y+first>=height: continue
                        local=y*width+x; pixel=(y+first)*width+x
                        self.assertLess(local,width*rows)
                        self.assertNotIn(pixel,written)
                        written.add(pixel)
            self.assertEqual(len(written),width*height)

    def test_stage_profiling_is_labelled_diagnostic_without_waits(self):
        source=(ROOT/'code/renderer_vulkan/pt_staged.h').read_text()
        profile=source.split('static void staged_profile_begin',1)[1].split('static void staged_dispatch',1)[0]
        self.assertNotIn('VK_QUERY_RESULT_WAIT_BIT',profile)
        self.assertNotIn('WaitIdle',profile)
        self.assertIn('PT_STAGED_PROFILE',profile)
        native=(ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('(pt.profile_instrumented || pt.staged.profile_written) ? "PT_PROFILE_DIAGNOSTIC"',native)

def verify_spirv(disassembler):
    for i in range(4):
        binary=SHADERS/f'Compiled/pt_staged_{i}.cspv'
        code=subprocess.check_output([disassembler,str(binary)],text=True,timeout=30)
        if not re.search(r'OpDecorate %\w*StagedState\w* ArrayStride 384',code):
            raise ValueError(f'{binary.name}: state stride does not match native allocation')
    print('PASS: all four production staged binaries use the 384-byte state ABI')

def verify_log(path):
    text=path.read_text(errors='replace')
    if 'PT_STAGED enabled=1 requested=2' not in text or 'PT_STAGED_UNAVAILABLE' in text:
        raise ValueError('Paired staged rendering did not run')
    rows=re.findall(r'PT_STAGED_COMPARE pixels=(\d+) mismatched=(\d+) nonfinite=(\d+) max_relative=(\S+) max_absolute=(\S+)',text)
    if not rows: raise ValueError('No synchronized raw-output comparison')
    for pixels,mismatched,nonfinite,relative,absolute in rows:
        print(f'Paired raw output: {pixels} pixels; {mismatched} outside tolerance; {nonfinite} nonfinite; max relative {relative}, absolute {absolute}')
        if int(pixels)<1920*1080 or int(nonfinite) or int(mismatched):
            raise ValueError('Staged output has NOT established equivalence')

def profile_log(path):
    text=path.read_text(errors='replace')
    if 'PT_CACHE_COMPLETE' not in text or 'PT_STAGED enabled=1 requested=1' not in text:
        raise ValueError('Incomplete staged diagnostic')
    rows=[{k:float(v) for k,v in re.findall(r'(\w+)=([\d.]+)',line)}
          for line in text.splitlines() if line.startswith('PT_STAGED_PROFILE ')]
    rows=rows[3:-3]
    if len(rows)<20: raise ValueError('Too few settled diagnostic frames')
    for row in rows:
        if abs(sum(row[k] for k in ('init','surface','lighting','advance','gaps'))-row['span'])>.005:
            raise ValueError('Inconsistent timestamp accounting')
    print(f'{len(rows)} settled diagnostic frames; timestamps perturb scheduling, NOT an FPS benchmark')
    for key in rows[0]:
        print(f'{key}: median {statistics.median(row[key] for row in rows):.3f}')

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--spirv-dis');parser.add_argument('--log',type=Path)
    parser.add_argument('--profile-log',type=Path)
    args=parser.parse_args()
    if args.spirv_dis: verify_spirv(args.spirv_dis)
    if args.log: verify_log(args.log)
    if args.profile_log: profile_log(args.profile_log)
    unittest.main(argv=[__file__])
