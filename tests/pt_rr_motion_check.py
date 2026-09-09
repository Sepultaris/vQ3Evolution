"""Matched stationary/pan timing and conservative moving RR budget acceptance."""
import argparse
import json
from pathlib import Path
import re
import statistics
import numpy as np
from PIL import Image


def read_run(directory):
    manifest = json.loads((directory/'settings.json').read_text(encoding='utf-8-sig'))
    name = directory.name.removeprefix('performance-')
    summary = json.loads((directory.parent/f'pt-perf-{name}.run.json').read_text(encoding='utf-8-sig'))
    log = (directory/'baseq3/qconsole.log').read_text(errors='replace')
    assert summary['completionMarker'] and summary['exitCode'] == 0 and not summary['timedOut']
    assert summary['sourceSettingsUnchanged'] and manifest['bounded'] and not manifest['synthetic']
    assert manifest['config'] == 'pt_rr_motion.cfg'
    expected = {'r_dlss': '5', 'r_dlssRayReconstruction': '1', 'r_dlssFrameGeneration': '0',
                'r_dlssNeuralRendering': '0', 'r_pathTracingSamples': '4', 'r_pathTracingBounces': '4'}
    for key, value in expected.items(): assert manifest['settings'][key] == value, key
    assert 'RR sampling: light reuse 1, adaptive 1, ceiling 4 spp' in log
    assert 'Ray Reconstruction active: 1920x1080 -> 1920x1080' in log
    assert 'Frame Generation: enabled 0, viewport active 0' in log
    assert not re.search(r'VUID-|SYNC-HAZARD-|Validation (?:Error|Warning)|'
                         r'\[debugUtilsMessengerCallback\]|Ray Reconstruction evaluation failed', log)
    if summary['validationLayerLoaded']:
        assert not (directory/'validation.log').read_text().strip()
    phase = None
    rows = {}
    for line in log.splitlines():
        if line.startswith('PT_RR_MOTION_'):
            phase = line.removeprefix('PT_RR_MOTION_')
            rows[phase] = []
        if line.startswith('PT_PROFILE ') and phase:
            rows[phase].append({k: float(v) for k, v in re.findall(r'(\w+)=([\d.]+)', line)})
    timings = {}
    for phase in ('STATIONARY', 'PAN'):
        assert len(rows[phase]) >= 25
        settled = rows[phase][3:-3]
        medians = {k: statistics.median(row[k] for row in settled) for k in settled[0]}
        timings[phase] = {'records': len(settled), 'median_ms': medians, 'fps': 1000/medians['frame']}
    fractions = {}
    for phase in ('stationary', 'pan', 'fast', 'cut'):
        a = np.asarray(Image.open(directory/'baseq3/screenshots'/f'rr_motion_budget_{phase}.tga'), dtype=float)[:-100]
        r, g, b = np.moveaxis(a, -1, 0)
        green = (g>150) & (g>r+70) & (b<90)
        red = (r>150) & (r>g+70) & (b<90)
        assert (green|red).mean() > .9, 'Expected budget visualization'
        fractions[phase] = float(green.sum()/(green|red).sum())
    cameras = re.findall(r'^\([^\n]+\) : [^\n]+$', log, re.MULTILINE)
    assert len(cameras) == 2 and cameras[0] != cameras[1]
    return manifest, cameras, {'run': name, 'validation': summary['validationLayerLoaded'],
                               'timings': timings, 'half_budget': fractions}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', nargs='+', type=Path)
    parser.add_argument('--accept', action='store_true', help='Check the final moving-budget/fast-turn/cut safeguards')
    args = parser.parse_args()
    identity = None
    outputs = []
    for p in args.runs:
        manifest, cameras, output = read_run(p)
        current = (manifest['sourceSha256'], manifest['config'], manifest['settings'], cameras)
        if identity is not None: assert identity == current, 'Mismatched settings or camera sequence'
        identity = current
        outputs.append(output)
    print(json.dumps(outputs, indent=2))
    if args.accept:
        f = outputs[-1]['half_budget']
        assert f['stationary'] > .2 and f['pan'] > .15, 'Insufficient verified reuse coverage'
        assert f['fast'] < .01 and f['cut'] < .01, 'Fast-turn/cut full-budget protection failed'
        assert any(o['validation'] for o in outputs), 'A clean validation run is required'
        print('PASS: tracked pan reduces sampling; fast turn and camera cut retain full sampling')
