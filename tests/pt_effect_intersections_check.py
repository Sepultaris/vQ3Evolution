"""Check matched, real GPU screenshots of nearly touching/intersecting effects.

Requires the captured broken baseline: both neutral cards should reveal sky in
that run. The corrected run must retain the backing's normals AND checkerboard.
ROIs exclude the weapon, HUD, effect edges and the exact coplanar crossing line.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from PIL import Image


def capture(run, name):
    value = np.asarray(Image.open(run/'baseq3/screenshots'/f'{name}.tga').convert('RGB'))
    assert value.shape == (1080, 1920, 3), 'This fixture is calibrated for native 1080p DLAA'
    return value


def check(before, after):
    manifests = [json.loads((p/'settings.json').read_text(encoding='utf-8-sig')) for p in (before, after)]
    assert manifests[0]['settings'] == manifests[1]['settings'], 'Quality settings differ'
    for run, manifest in zip((before, after), manifests):
        s = manifest['settings']
        for name, expected in [('r_dlss', '5'), ('r_dlssRayReconstruction', '1'),
                               ('r_dlssFrameGeneration', '0'), ('r_pathTracingSamples', '2')]:
            assert s[name] == expected, (name, s[name])
        log = (run/'baseq3/qconsole.log').read_text(errors='replace')
        assert 'PT_PROGRAM_COMPLETE' in log and 'PT_EFFECT_INTERSECTIONS' in log
        assert 'VUID-' not in log and 'Validation Error' not in log
        validation = run/'validation.log'
        assert validation.exists(), 'Validation capture missing'
        assert not validation.read_text(errors='replace').strip(), 'Validation messages present'
    old = capture(before, 'effect_intersections_normals')
    new = capture(after, 'effect_intersections_normals')
    color = capture(after, 'effect_intersections')
    expected = np.median(new[200:600, 800:850], axis=(0, 1))
    assert expected[1] > 100 and expected[2] > 100
    for name, box in [('additive', (250, 200, 730, 640)), ('crossing filter', (1180, 200, 1400, 640))]:
        x1, y1, x2, y2 = box
        a, b, c = [im[y1:y2, x1:x2].astype(float) for im in (old, new, color)]
        missing_before = np.mean(np.max(a, axis=2) < 5)
        restored = np.mean(np.max(np.abs(b-expected), axis=2) <= 1)
        # Both colors are necessary: a flat fill or sky cannot pass as a wall.
        yellow = np.mean((c[..., 0] > 150) & (c[..., 2] < 100))
        blue = np.mean((c[..., 2] > 100) & (c[..., 0] < 100))
        assert missing_before > .99, (name, 'negative control did not reproduce', missing_before)
        assert restored > .999, (name, 'background still missing', restored)
        assert yellow > .25 and blue > .25, (name, 'checkerboard not restored', yellow, blue)
        print(f'PASS: {name}: {missing_before:.1%} missing before; {restored:.1%} correct backing normals after; both checker colors visible')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    args = parser.parse_args()
    check(args.before, args.after)
