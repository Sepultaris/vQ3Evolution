"""Compile real UI-target routing and check fresh native-menu screenshot placement."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def native_test(cc):
    code = (ROOT / 'code/renderer_vulkan/tr_cmds.c').read_text()
    code = code[code.index('void RB_StretchPic('):code.index('static void R_PerformanceCounters(')]
    with tempfile.TemporaryDirectory(prefix='vq3-ui-target-') as folder:
        include = Path(folder) / 'pt_ui_target_code.inc'
        include.write_text(code)
        exe = str(Path(folder) / 'target.exe')
        command = [cc, str(ROOT / 'tests/pt_ui_target_fixture.c'), '-std=c11', '-O1', '-I', folder, '-o', exe]
        subprocess.run(command, check=True, timeout=30)
        subprocess.run([exe], check=True, timeout=10)
        # Negative control: the old projection-gated transition must reproduce
        # the consecutive-menu regression. Never modify the production source.
        old = code.replace('\tvk_temporal_begin_ui();', '', 1).replace(
            'if ( qfalse == backEnd.projection2D )\n    {',
            'if ( qfalse == backEnd.projection2D )\n    {\n        vk_temporal_begin_ui();', 1)
        assert old != code
        include.write_text(old)
        subprocess.run(command, check=True, timeout=30)
        result = subprocess.run([exe], capture_output=True, text=True, timeout=10)
        assert result.returncode == 1 and 'frame 1 retained scene target' in result.stderr, result
        print('PASS: old projection-gated transition fails the same regression test')


def image_test(directory, expected_broken):
    import numpy as np
    from PIL import Image
    manifest = json.loads((directory / 'settings.json').read_text(encoding='utf-8-sig'))
    name = directory.name.removeprefix('performance-')
    summary = json.loads((directory.parent / f'pt-perf-{name}.run.json').read_text(encoding='utf-8-sig'))
    log = (directory / 'baseq3/qconsole.log').read_text(errors='replace')
    assert summary['sourceSettingsUnchanged'] and not summary['timedOut'] and summary['exitCode'] == 0
    for marker in ('UI_TEST_MENU setup', 'UI_TEST_MENU graphics', 'UI_TEST_MENU close', 'PT_PROGRAM_COMPLETE'):
        assert marker in log, marker
    assert 'Unknown command' not in log
    rgb = np.asarray(Image.open(directory / 'baseq3/screenshots/ui_setup.jpg').convert('RGB'), dtype=np.int16)
    h,w,_ = rgb.shape
    # The topmost red pixels are the SETUP banner, separated from the art and
    # controls by black space. Locate its center without resizing/cropping files.
    red = (rgb[:,:,0]>130) & (rgb[:,:,0]>rgb[:,:,1]*2+40) & (rgb[:,:,0]>rgb[:,:,2]*2+40)
    rows = np.flatnonzero(red.sum(axis=1)>20)
    assert len(rows), 'Missing setup banner'
    top = int(rows[0]); ys,xs = np.where(red[top:top+max(10,int(h*.04))])
    center = (int(xs.min())+int(xs.max()))*.5
    scale = min(w/640,h/480)*float(manifest['settings']['ui_scale'])
    bias_y = (h-480*scale)*.5
    if expected_broken:
        assert center>w*.65 and top>bias_y+16*scale+60, (center,top)
    else:
        assert abs(center-w*.5)<3, (center,w)
        assert abs(top-(bias_y+16*scale))<10, (top,bias_y,scale)
    print(f'PASS: {name}: banner center x={center:.1f}, top y={top}, expected {"reported displacement" if expected_broken else "centered UI"}')
    if summary['validationLayerLoaded']:
        assert not (directory/'validation.log').read_text().strip()
        assert '[debugUtilsMessengerCallback]' not in log
        print('PASS: validation loaded, no diagnostics; saved settings unchanged')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='C:/msys64/ucrt64/bin/gcc.exe')
    parser.add_argument('--before', type=Path)
    parser.add_argument('--after', type=Path, nargs='*')
    args = parser.parse_args()
    native_test(args.cc)
    if args.before: image_test(args.before, True)
    for run in args.after or []: image_test(run, False)
