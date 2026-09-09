"""Check the tiled RR display pass and compare lossless post-only game captures."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def tone(hdr, exposure):
    x = np.where(np.isfinite(hdr).all(axis=-1, keepdims=True), hdr, 0)
    x = np.maximum(x * np.float32(exposure), 0)
    return np.clip((x*(2.51*x+.03))/(x*(2.43*x+.59)+.14), 0, 1)**np.float32(1/2.2)


def sharpen(c, n, s, e, w, sharpness):
    lo = np.minimum(c, np.minimum(np.minimum(n, s), np.minimum(e, w)))
    hi = np.maximum(c, np.maximum(np.maximum(n, s), np.maximum(e, w)))
    a = np.sqrt(np.clip(np.minimum(lo, 1-hi)/np.maximum(hi, 1/65535), 0, 1))
    weight = a * np.float32(-.2*sharpness)
    return np.clip((c+weight*(n+s+e+w))/(1+4*weight), 0, 1)


class PostTests(unittest.TestCase):
    def test_barrier_precedes_partial_group_return(self):
        source = (ROOT/'code/renderer_vulkan/shaders/pt_rr_post.comp').read_text()
        self.assertIn('shared vec3 displayTile[100];', source)
        self.assertIn('i=gl_LocalInvocationIndex;i<100u;i+=64u', source)
        main = source[source.index('void main()'):]
        self.assertLess(main.index('barrier();'), main.index('return;'))
        self.assertEqual(main.count('displayColor('), 2)  # Halo load; unsharpened path.

    def test_all_neighbors_and_partial_tiles_match_original(self):
        rng = np.random.default_rng(109)
        for width, height in ((1, 1), (2, 3), (7, 9), (8, 8), (17, 31)):
            hdr = rng.uniform(-.5, 30, (height, width, 3)).astype(np.float32)
            hdr[0, 0] = np.nan
            hdr[-1, -1] = np.inf
            for exposure in (.01, 1, 11.313708, 16):
                for strength in (0, .5, 1):
                    expected = tone(hdr, exposure)
                    padded = np.pad(expected, ((1, 1), (1, 1), (0, 0)), mode='edge')
                    if strength:
                        expected = sharpen(expected, padded[:-2, 1:-1], padded[2:, 1:-1],
                                           padded[1:-1, 2:], padded[1:-1, :-2], strength)
                    actual = np.zeros_like(expected)
                    for y in range(0, height, 8):
                        for x in range(0, width, 8):
                            tile = np.zeros((100, 3), dtype=np.float32)
                            writes = np.zeros(100, dtype=int)
                            for lane in range(64):
                                for i in range(lane, 100, 64):
                                    px, py = min(max(x+i%10-1, 0), width-1), min(max(y+i//10-1, 0), height-1)
                                    tile[i] = tone(hdr[py, px], exposure)
                                    writes[i] += 1
                            np.testing.assert_array_equal(writes, 1)
                            for ly in range(min(8, height-y)):
                                for lx in range(min(8, width-x)):
                                    c = (ly+1)*10+lx+1
                                    actual[y+ly, x+lx] = sharpen(tile[c], tile[c-10], tile[c+10],
                                                               tile[c+1], tile[c-1], strength) if strength else tile[c]
                    np.testing.assert_array_equal(actual, expected)


def compare(before, after):
    manifests = [json.loads((p/'settings.json').read_text(encoding='utf-8-sig')) for p in (before, after)]
    for key in ('config', 'sourceSha256', 'settings', 'executableSha256'):
        assert manifests[0][key] == manifests[1][key], f'Mismatched {key}'
    for p in (before, after):
        summary = json.loads((p.parent/('pt-perf-'+p.name.removeprefix('performance-')+'.run.json')).read_text(encoding='utf-8-sig'))
        assert summary['completionMarker'] and not summary['timedOut'] and summary['sourceSettingsUnchanged']
    files = sorted((before/'baseq3/screenshots').glob('rr_motion_*.tga'))
    assert len(files) >= 8, 'Missing movement/edge/cut captures'
    for file in files:
        old = np.asarray(Image.open(file))
        new = np.asarray(Image.open(after/'baseq3/screenshots'/file.name))
        difference = np.abs(new.astype(float)-old)
        # Separate game lifetimes include stochastic RR/history differences.
        # This is a broad image guard, NOT a shader equivalence proof; the
        # same-input headless GPU fixture supplies the strict byte-exact check.
        assert difference.mean() < 1, (file.name, difference.mean())
        print(f'{file.name}: mean channel difference {difference.mean():.4f}/255')
    print(f'PASS: {len(files)} lossless game captures within broad image bounds (not pixel equivalence)')


def gpu_check(cxx, sdk):
    with tempfile.TemporaryDirectory(prefix='pt-rr-post-gpu-') as folder:
        folder = Path(folder)
        reference, exe = folder/'reference.cspv', folder/'check.exe'
        subprocess.run([str(sdk/'Bin/glslangValidator.exe'), '--target-env', 'vulkan1.2', '-V',
                        '-I'+str(ROOT/'code/renderer_vulkan/shaders'), str(ROOT/'tests/pt_rr_post_reference.comp'),
                        '-o', str(reference)], check=True, timeout=20)
        subprocess.run([str(sdk/'Bin/spirv-opt.exe'), '--target-env=vulkan1.2', '-O', str(reference),
                        '-o', str(reference)], check=True, timeout=20)
        subprocess.run([cxx, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-missing-field-initializers',
                        str(ROOT/'tests/pt_rr_post_gpu.cpp'), '-I', str(sdk/'Include'), '-L', str(sdk/'Lib'),
                        '-l:vulkan-1.lib', '-o', str(exe)], check=True, timeout=30)
        validation = folder/'validation.log'
        (folder/'vk_layer_settings.txt').write_text(
            'khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG\n'
            f'khronos_validation.log_filename = {validation.as_posix()}\n'
            'khronos_validation.report_flags = error,warn\n'
            'khronos_validation.validate_sync = true\n')
        env = dict(os.environ, VK_INSTANCE_LAYERS='VK_LAYER_KHRONOS_validation',
                   VK_LAYER_PATH=str(sdk/'Bin'), VK_LAYER_SETTINGS_PATH=str(folder))
        subprocess.run([str(exe), str(reference), str(ROOT/'code/renderer_vulkan/shaders/Compiled/pt_rr_post.cspv')],
                       check=True, timeout=25, env=env)
        assert validation.exists(), 'Validation layer did not open its log'
        assert not validation.read_text().strip(), validation.read_text()
        print('PASS: identical-input GPU output is byte-exact; Vulkan/synchronization validation clean')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compare', nargs=2, type=Path)
    parser.add_argument('--gpu', action='store_true', help='Explicitly run the headless 25-second-capped GPU fixture')
    parser.add_argument('--cxx')
    parser.add_argument('--sdk', type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(PostTests))
    if not result.wasSuccessful(): raise SystemExit(1)
    if args.compare: compare(*args.compare)
    if args.gpu:
        assert args.cxx and args.sdk, '--gpu requires --cxx and --sdk'
        gpu_check(args.cxx, args.sdk)
