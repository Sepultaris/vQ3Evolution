"""Measure global subpixel displacement in a fixed-camera frame sequence."""
import argparse
import json
from pathlib import Path
import re
import unittest
import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def fit_offsets(patches):
    template = patches.mean(axis=0)
    gy, gx = np.gradient(template)
    strength = gx*gx + gy*gy
    mask = strength > np.percentile(strength, 70)
    gradient = np.stack([gx[mask], gy[mask]], axis=1)
    assert np.linalg.matrix_rank(gradient) == 2, 'Patch needs texture on both axes'
    # Small-displacement optical-flow fit to a shared sequence template.
    return np.array([np.linalg.lstsq(gradient, (template-p)[mask], rcond=None)[0]
                     for p in patches])


def read_run(directory):
    summary = json.loads((directory.parent / ('pt-perf-' + directory.name.removeprefix('performance-')
                                             + '.run.json')).read_text(encoding='utf-8-sig'))
    manifest = json.loads((directory / 'settings.json').read_text(encoding='utf-8-sig'))
    log = (directory / 'baseq3/qconsole.log').read_text(errors='replace')
    assert summary['completionMarker'] and not summary['timedOut']
    assert summary['sourceSettingsUnchanged'] and summary['exitCode'] == 0
    assert manifest['bounded'] and not manifest['synthetic']
    assert manifest['config'] == 'pt_rr_jitter.cfg'
    for name, value in {'r_dlss': '5', 'r_dlssRayReconstruction': '1',
                        'r_dlssFrameGeneration': '0', 'r_dlssNeuralRendering': '0',
                        'r_pathTracingAdaptive': '1', 'r_pathTracingLightReuse': '1'}.items():
        assert manifest['settings'][name] == value, (name, manifest['settings'][name])
    assert 'PT_JITTER_STATIONARY_BEGIN' in log and 'PT_PROGRAM_COMPLETE' in log
    assert 'Ray Reconstruction active: 1920x1080 -> 1920x1080' in log
    assert 'Ray Reconstruction: supported 1, enabled 1' in log
    assert 'Frame Generation: enabled 0, viewport active 0' in log
    assert 'RR sampling: light reuse 1, adaptive 1, ceiling 4 spp' in log
    assert not re.search(r'VUID-|SYNC-HAZARD-|Validation (?:Error|Warning)|'
                         r'\[debugUtilsMessengerCallback\]|Ray Reconstruction evaluation failed', log)
    if summary['validationLayerLoaded']:
        assert not (directory / 'validation.log').read_text().strip()
    return manifest, summary


def measure(directory):
    files=sorted((directory/'baseq3/screenshots').glob('jitter_*.jpg'))
    assert len(files)==16, f'Expected 16 consecutive frames, got {len(files)}'
    assert [p.stem for p in files] == [f'jitter_{i:02d}' for i in range(16)]
    read_run(directory)
    frames=[np.asarray(Image.open(p).convert('L'),dtype=float) for p in files]
    assert all(f.shape == (1080, 1920) for f in frames)
    # Two separate static textured patches, no HUD, sky, or animated entities.
    results={}
    for name,(x,y,w,h) in {'circuit':(1300,80,460,620),'wall':(100,80,380,620)}.items():
        patches=np.stack([f[y:y+h,x:x+w] for f in frames])
        offsets=fit_offsets(patches)
        results[name]={'span_xy':np.ptp(offsets,axis=0).tolist(),
                       'rms':float(np.sqrt(np.mean(np.sum(offsets*offsets,axis=1)))),
                       'offsets':offsets.round(4).tolist()}
    return results


def assert_stable(result):
    for patch, (max_x, max_y, max_rms) in {'circuit': (.3, .2, .1), 'wall': (.6, .2, .15)}.items():
        assert result[patch]['span_xy'][0] < max_x, result[patch]
        assert result[patch]['span_xy'][1] < max_y, result[patch]
        assert result[patch]['rms'] < max_rms, result[patch]


def compare(before, after):
    old_manifest, _ = read_run(before)
    new_manifest, summary = read_run(after)
    for key in ('sourceSha256', 'config', 'settings', 'executableSha256'):
        assert old_manifest[key] == new_manifest[key], f'Mismatched {key}'
    assert old_manifest['rendererSha256'] != new_manifest['rendererSha256']
    assert summary['validationLayerLoaded'], 'Fixed build must also pass Vulkan validation'
    old, new = measure(before), measure(after)
    assert_stable(new)
    for patch in new:
        assert new[patch]['rms'] < old[patch]['rms'] * .5, (old[patch], new[patch])
        print(f"PASS: {patch}: fitted RMS displacement {old[patch]['rms']:.4f} -> "
              f"{new[patch]['rms']:.4f} pixels (all 16 frames); settings matched, validation clean")


class JitterTests(unittest.TestCase):
    def test_production_projection_and_compensation_contract(self):
        temporal = (ROOT / 'code/renderer_vulkan/vk_temporal.c').read_text()
        streamline = (ROOT / 'code/renderer_vulkan/vk_streamline.cpp').read_text()
        main = (ROOT / 'code/renderer_vulkan/tr_main.c').read_text()
        for axis, index, extent in (('x', 8, 'width'), ('y', 9, 'height')):
            update = (f'projection_matrix[{index}] += 2.0f * temporal.jitter_{axis} / '
                      f'(float)temporal.render_{extent};')
            self.assertIn(update, temporal)
            self.assertLess(temporal.index('memcpy(temporal.projection, projection_matrix'),
                            temporal.index(update))
        self.assertIn('projectionMatrix[11] = -1.0f;', main)
        self.assertIn('constants.jitterOffset = sl::float2(-r->jitter_x, -r->jitter_y);', streamline)
        self.assertIn('constants.motionVectorsJittered = sl::Boolean::eFalse;', streamline)

    def test_projection_pixel_displacement_and_old_sign_negative_control(self):
        # Renderer column vectors: adding P[8:9] contributes jitter*z to clip.xy,
        # while clip.w=-z. Vulkan viewport transform has positive width/height.
        for width, height in ((1920, 1080), (3440, 1440), (1280, 720)):
            for jitter in ((.375, -.3888889), (-.4375, .1666667), (0, 0)):
                for z in (-4., -100., -4096.):
                    image_shift = np.array([(2*j/extent*z)/(-z)*extent*.5
                                            for j, extent in zip(jitter, (width, height))])
                    np.testing.assert_allclose(image_shift, -np.array(jitter), atol=1e-12)
                    np.testing.assert_allclose(image_shift - (-np.array(jitter)), 0, atol=1e-12)
                    # Previously reported +jitter leaves exactly -2*jitter wobble.
                    np.testing.assert_allclose(image_shift - jitter, -2*np.array(jitter), atol=1e-12)

    def test_displacement_measurement_calibration(self):
        y, x = np.mgrid[:160, :240]
        shifts = np.array([[-.4, -.2], [.4, .2], [-.2, .3], [.2, -.3]])
        patches = np.array([100 + 30*np.sin((x-dx)*.11) + 20*np.cos((y-dy)*.14)
                            + 15*np.sin((x-dx)*.07 + (y-dy)*.09) for dx, dy in shifts])
        np.testing.assert_allclose(fit_offsets(patches), shifts, atol=.003)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs',nargs='*',type=Path)
    parser.add_argument('--compare', nargs=2, type=Path, metavar=('BEFORE', 'AFTER'))
    parser.add_argument('--accept', type=Path, help='Check established absolute stability bounds without claiming a matched A/B')
    args=parser.parse_args()
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(JitterTests))
    if not result.wasSuccessful(): raise SystemExit(1)
    for p in args.runs: print(json.dumps({'run':p.name,'motion_pixels':measure(p)},indent=2))
    if args.compare: compare(*args.compare)
    if args.accept:
        _, summary = read_run(args.accept)
        assert summary['validationLayerLoaded']
        assert_stable(measure(args.accept))
        print('PASS: absolute stationary jitter bounds and clean validation (not a matched A/B)')
