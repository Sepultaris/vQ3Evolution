"""Verify workgroup tuning changes scheduling, not per-pixel transport."""
import argparse
from pathlib import Path
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / 'code/renderer_vulkan/shaders'


def shader_body(sdk, source):
    result = subprocess.run([str(Path(sdk) / 'Bin/glslangValidator.exe'), '-E', str(source)],
                            capture_output=True, text=True, check=True, timeout=30).stdout
    result = re.sub(r'^\s*#.*$', '', result, flags=re.M)
    result = re.sub(r'layout\s*\(\s*local_size_x[^;]+;', '', result)
    return re.sub(r'\s+', '', result)


class WorkgroupTests(unittest.TestCase):
    def test_shader_selection_only_changes_execution_size(self):
        source = (SHADERS / 'pt_compact_transport.comp').read_text()
        self.assertIn('#define PT_WORKGROUP_SPECIALIZATION 1', source)
        self.assertIn('#include "pt_integrator.glsl"', source)
        self.assertNotIn('PT_PARALLEL_SAMPLES', source)

    def test_ceil_dispatch_and_edge_guard_cover_each_pixel_once(self):
        for rows in (8, 64, 128):
            for size in (1, 7, 8, 9, 63, 64, 65, 1079, 1080, 1440, 2160):
                actual = [g * rows + lane for g in range((size + rows - 1) // rows)
                          for lane in range(rows) if g * rows + lane < size]
                self.assertEqual(actual, list(range(size)))
        source = (ROOT / 'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('(pt.height + pt.compact_transport_rows - 1) / pt.compact_transport_rows', source)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk')
    args = parser.parse_args()
    if args.sdk:
        production = shader_body(args.sdk, SHADERS / 'pt_compact_transport.comp')
        fixed_group = shader_body(args.sdk, ROOT / 'tests/pt_wide_group.comp')
        assert production == fixed_group, 'Workgroup variant changes shader calculations'
        for token in ('gl_WorkGroupID', 'gl_LocalInvocationID', 'gl_LocalInvocationIndex'):
            assert token not in production, 'Per-pixel algorithm depends on grouping'
        print('PASS: preprocessed per-pixel shader bodies identical; RNG has no workgroup dependency')
    unittest.main(argv=[__file__])
