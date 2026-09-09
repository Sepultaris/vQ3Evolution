"""Verify same-math function hints, retained calls, and native variant selection."""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / 'code/renderer_vulkan/shaders'
FUNCTIONS = {'layerSampleUV', 'layerSample', 'materialColor', 'emissionAt', 'applyDecals', 'trace', 'visibility'}


def instructions(words):
    at = 5
    while at < len(words):
        count, opcode = words[at] >> 16, words[at] & 65535
        assert count and at + count <= len(words)
        yield at, count, opcode
        at += count


def run(sdk):
    sdk = Path(sdk) / 'Bin'
    with tempfile.TemporaryDirectory(prefix='pt-shared-functions-') as temp:
        temp = Path(temp)
        for variant in ('pathtrace', 'pt_brdf', 'pt_profile', 'pt_profile_brdf'):
            original, hinted = temp / 'original.spv', temp / 'hinted.spv'
            source = SHADERS / (variant + '.comp')
            subprocess.run([str(sdk/'glslangValidator.exe'), '--target-env', 'vulkan1.2', '-V', str(source), '-o', str(original)], check=True, capture_output=True, timeout=30)
            subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(ROOT/'tools/pt-shader-variant.ps1'),
                            '-VulkanSDK', str(sdk.parent), '-Source', str(source), '-Output', str(hinted), '-HintsOnly'], check=True, capture_output=True, timeout=30)
            raw, changed = original.read_bytes(), hinted.read_bytes()
            assert len(raw) == len(changed)
            before = list(struct.unpack('<'+'I'*(len(raw)//4), raw))
            after = list(struct.unpack('<'+'I'*(len(changed)//4), changed))
            edits = {i for i, (a,b) in enumerate(zip(before,after)) if a != b}
            names, allowed, marked = {}, set(), set()
            for at, count, op in instructions(before):
                if op == 5:
                    names[before[at+1]] = raw[(at+2)*4:(at+count)*4].split(b'\0')[0].decode().split('(')[0]
                if op == 54 and names.get(before[at+2]) in FUNCTIONS:
                    allowed.add(at+3)
                    marked.add(names[before[at+2]])
                    assert after[at+3] == (before[at+3] & ~1) | 2
            assert marked == FUNCTIONS and edits == allowed, 'Modification beyond selected function-control hints'
            # Rejected runtime experiment remains reproducible in a temporary
            # directory, not linked into the renderer or used by its build.
            binary = hinted
            subprocess.run([str(sdk/'spirv-opt.exe'), '--target-env=vulkan1.2', '-O', str(binary), '-o', str(binary)], check=True, timeout=30)
            assembly = subprocess.run([str(sdk/'spirv-dis.exe'), str(binary)], check=True, capture_output=True, text=True).stdout
            retained = set(re.findall(r'(%\w+)\s*=\s*OpFunction\s+%\w+\s+DontInline', assembly))
            calls = set(re.findall(r'OpFunctionCall\s+%\w+\s+(%\w+)', assembly))
            assert len(retained) >= 8 and retained <= calls, 'Optimizer erased shared function boundaries'
            subprocess.run([str(sdk/'spirv-val.exe'), '--target-env', 'vulkan1.2', str(binary)], check=True, timeout=30)
            print(f'PASS: {variant}: only {len(edits)} function hints changed; {len(retained)} shared callees retained')


class SharedFunctionsTests(unittest.TestCase):
    def test_same_integrator_and_profile_math(self):
        for name in ('pt_light_loop', 'pt_light_loop_brdf', 'pt_light_loop_profile', 'pt_light_loop_profile_brdf'):
            source = (SHADERS/(name+'.comp')).read_text()
            self.assertIn('#include "pt_integrator.glsl"', source)
            self.assertEqual('PT_BRDF_REUSE' in source, 'brdf' in name)
            self.assertEqual('PT_PROFILE_PASS' in source, 'profile' in name)
            self.assertNotIn('void ', source)
            self.assertIn('#define PT_UNIFIED_LIGHTS 1', source)

    def test_shared_mode_has_native_fallback_and_matching_instrumentation(self):
        native = (ROOT/'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('if (requested & 16) return lighting_pipeline_select(requested & 15);', native)
        self.assertIn('r_pathTracingLightLoop->integer ? 16u : 0u', native)
        self.assertIn('PT_LIGHT_LOOP enabled=%d requested=%d', native)
        profile = (ROOT/'code/renderer_vulkan/pt_shader_profile.h').read_text()
        self.assertIn('pt_shader_profile.light_loop == light_loop', profile)
        self.assertIn('pt_light_loop_profile_brdf_comp_spv : pt_light_loop_profile_comp_spv', profile)

    def test_compilation_tool_never_renders(self):
        source = (ROOT/'tests/pt_compile_stats.c').read_text()
        for call in ('vkQueueSubmit(', 'vkCmdDispatch(', 'vkCreateSwapchainKHR(', 'vkAllocateMemory(', 'vkCreateBuffer('):
            self.assertNotIn(call, source)
        self.assertIn('#include "../code/renderer_vulkan/pt_pipeline_stats.h"', source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', required=True)
    args = parser.parse_args()
    run(args.sdk)
    unittest.main(argv=[__file__])
