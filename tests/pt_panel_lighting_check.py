"""Stock monitor semantics and optional matched Q3DM0 GPU lighting captures.

The executable native/GLSL mask tests live in pt_material_layers_check.py.
GPU image checks supplement, not replace, visual inspection.
"""
import argparse
import io
import json
from pathlib import Path
import re
import zipfile


def assets(pak):
    import numpy as np
    from PIL import Image

    with zipfile.ZipFile(pak) as z:
        blocks = {}
        for name in z.namelist():
            if not name.startswith('scripts/') or not name.endswith('.shader'):
                continue
            tokens = re.findall(r'[^\s{}]+|[{}]', re.sub(r'//[^\r\n]*', '', z.read(name).decode('latin1')))
            depth = 0
            for i, token in enumerate(tokens):
                if token == '{':
                    if depth == 0:
                        start = i - 1
                    depth += 1
                elif token == '}':
                    depth -= 1
                    if depth == 0:
                        blocks[tokens[start]] = ' '.join(tokens[start:i+1]).lower()
        for name in ('comp3', 'comp3b'):
            shader = blocks[f'textures/base_wall/{name}']
            assert 'q3map_surfacelight 1000' in shader
            assert 'tcmod scroll 3 ' in shader
            assert f'map textures/base_wall/{name}.tga blendfunc gl_src_alpha gl_one_minus_src_alpha' in shader
            texture = np.asarray(Image.open(io.BytesIO(z.read(f'textures/base_wall/{name}.tga'))))
            assert texture.shape[2] == 4
            alpha = texture[:, :, 3]
            # The glass has a low-alpha sheen (mostly 22/255), not binary
            # zero coverage. Preserve that authored partial transmission.
            assert (alpha < 64).mean() > .2 and (alpha == 255).mean() > .2
    print('PASS: installed monitor assets separate scrolling surface-light displays from opaque/transparent foreground frame texels')


def gpu(before, after):
    import numpy as np
    from PIL import Image

    manifests = []
    frames = []
    for directory in (before, after):
        label = directory.name.removeprefix('performance-')
        summary = json.loads((directory.parent / f'pt-perf-{label}.run.json').read_text(encoding='utf-8-sig'))
        manifest = json.loads((directory / 'settings.json').read_text(encoding='utf-8-sig'))
        manifests.append(manifest)
        assert summary['exitCode'] == 0 and not summary['timedOut'] and summary['completionMarker']
        assert summary['elapsedSeconds'] <= 45 and summary['sourceSettingsUnchanged']
        assert manifest['map'] == 'q3dm0' and manifest['config'] == 'pt_panel_lighting.cfg'
        for key, value in [('r_dlss', '5'), ('r_pathTracingSamples', '2'), ('r_pathTracingAdaptive', '0'),
                           ('r_dlssFrameGeneration', '0'), ('r_dlssNeuralRendering', '0')]:
            assert manifest['settings'][key] == value
        log = (directory / 'baseq3/qconsole.log').read_text(errors='replace')
        assert 'Ray Reconstruction: supported 1, enabled 1' in log
        assert not re.search(r'Unknown command|VUID-|SYNC-HAZARD|debugUtilsMessengerCallback|Ray Reconstruction evaluation failed', log, re.I)
        captures = []
        for pose in ('front', 'animated'):
            path = directory / f'baseq3/screenshots/panels_{pose}.tga'
            image = Image.open(path)
            image.save(path.with_suffix('.png'))
            assert image.size == (1920, 1080), 'Measured frame ROIs require the original 1080p test camera'
            pixels = np.asarray(image, dtype=float)
            # Upper metal rims of the two monitors; no animated display/HUD.
            captures.append([pixels[417:429, 395:610, :3].mean(), pixels[417:429, 1324:1539, :3].mean()])
        frames.append(captures)
        if directory == after:
            assert summary['validationLayerLoaded']
            assert not (directory / 'validation.log').read_text().strip()
    assert manifests[0]['settings'] == manifests[1]['settings']
    assert manifests[0]['sourceSha256'] == manifests[1]['sourceSha256']
    assert manifests[0]['rendererSha256'] != manifests[1]['rendererSha256']
    old, new = np.asarray(frames)
    assert np.all(old > 100), old
    assert np.all((new > 5) & (new < old * .7)), (old, new)
    print(f'PASS: both frame rims stop glowing but remain visible in both captures; mean RGB {old.mean():.1f} -> {new.mean():.1f}/255')
    print('PASS: matched DLAA/RR/two-sample quality, clean Vulkan, unchanged saved settings and bounded clean shutdowns')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pak', type=Path)
    parser.add_argument('--before', type=Path)
    parser.add_argument('--after', type=Path)
    args = parser.parse_args()
    if bool(args.before) != bool(args.after):
        parser.error('--before and --after must be supplied together')
    if not args.pak and not args.before:
        parser.error('Supply --pak and/or the paired GPU capture directories')
    if args.pak:
        assets(args.pak)
    if args.before:
        gpu(args.before, args.after)
