"""Accept only completed, same-camera, real-frame GPU benchmarks."""
import argparse
import json
from pathlib import Path
import re
import statistics
import unittest
from pt_material_program_check import parse


def summarize(home):
    home = Path(home)
    manifest = json.loads((home / 'settings.json').read_text(encoding='utf-8-sig'))
    text = (home / 'baseq3/qconsole.log').read_text(errors='replace')
    guard = (home / 'guard.log').read_text(errors='replace')
    if not re.search(r'VQ3E_BOUNDED exit=0 elapsed_ms=\d+ timed_out=0', guard):
        raise ValueError('No successful guarded process exit')
    if manifest['synthetic'] or manifest['settings']['r_dlssFrameGeneration'] != '0':
        raise ValueError('Must use saved quality with Frame Generation off')
    if 'Frame Generation: enabled 0, viewport active 0, queries 0, presented 0' not in text:
        raise ValueError('Frame Generation was not verified inactive at runtime')
    views = [tuple(map(int, v)) for v in re.findall(r'^\((-?\d+) (-?\d+) (-?\d+)\) : (-?\d+)$', text, re.M)]
    # TeleportPlayer launches at 400 units/s; with fixedtime 16 the requested
    # x=-1510 settles at -1395. Check the resulting camera, not the command.
    if len(views) != 2 or views[0] != views[1] or views[0] != (-1395, 200, 50, 0):
        raise ValueError(f'Camera not verified before/after timing: {views}')
    rows = parse(text, phases=('REFERENCE_A',))['REFERENCE_A']
    medians = {k: statistics.median(r[k] for r in rows) for k in rows[0]}
    return {'home': str(home), 'samples': len(rows), 'camera': views[0], 'milliseconds': medians,
            'realFPS': 1000 / medians['frame'], 'settings': manifest['settings'],
            'sourceSha256': manifest['sourceSha256'], 'rendererSha256': manifest['rendererSha256']}


class BenchmarkPolicyTests(unittest.TestCase):
    def test_runner_forces_fg_off_in_config_and_startup(self):
        source = Path(__file__).with_name('run-pt-performance.ps1').read_text()
        self.assertIn("$ptSettings['r_dlssFrameGeneration'] = '0'", source)
        self.assertIn("$ptArguments += ' +set r_dlssFrameGeneration 0'", source)
        self.assertIn('benchmarkOverrides = $ptBenchmarkOverrides', source)
        self.assertIn('sourceSettingsUnchanged', source)

    def test_camera_and_capture_are_bracketed(self):
        cfg = Path(__file__).with_name('pt_query_state.cfg').read_text()
        self.assertEqual(cfg.count('viewpos\n'), 2)
        self.assertLess(cfg.index('wait 80'), cfg.index('setviewpos'))
        self.assertIn('screenshotJPEG query_state\nwait 4', cfg)
        for setting in ('Samples', 'Bounces', 'Exposure', 'Denoise'):
            self.assertNotIn('set r_pathTracing' + setting, cfg)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('home', type=Path, nargs='*')
    args = parser.parse_args()
    if args.home:
        results = [summarize(home) for home in args.home]
        first = results[0]
        for current in results[1:]:
            differences = {k for k in first['settings'] | current['settings']
                           if first['settings'].get(k) != current['settings'].get(k)}
            if differences - {'r_dlssNeuralRendering'}:
                raise ValueError(f'Quality mismatch: {differences}')
            if current['sourceSha256'] != first['sourceSha256']:
                raise ValueError('Saved source settings changed between runs')
        print(json.dumps(results, indent=2))
    else:
        unittest.main(argv=[__file__])
