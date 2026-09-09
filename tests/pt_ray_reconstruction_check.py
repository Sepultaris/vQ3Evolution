"""RR input/pipeline contracts and fresh, matched GPU run evidence (not a visual-quality proof)."""
import argparse
import json
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHADERS = ROOT / 'code/renderer_vulkan/shaders'


class RayReconstructionTests(unittest.TestCase):
    def test_raw_linear_input_and_post_exposure(self):
        pack = (SHADERS / 'pt_rr_pack.comp').read_text()
        for signal in ('diffuse[i].rgb', 'specular[i].rgb', 'transmission[i].rgb', 'emission[i].rgb'):
            self.assertIn(signal, pack)
        self.assertNotIn('pow(', pack)
        self.assertNotIn('history[', pack)
        self.assertIn('VK_FORMAT_R16G16B16A16_SFLOAT', (ROOT / 'code/renderer_vulkan/pt_ray_reconstruction.h').read_text())
        self.assertIn('x*pc.sunExposure.w', (SHADERS / 'pt_rr_post.comp').read_text())

    def test_reflection_query_has_secondary_visibility(self):
        source = (SHADERS / 'pt_integrator.glsl').read_text()
        # No primary-weapon priority or camera near clip on a reflected ray.
        self.assertIn('specDirection,0.001,pc.parameters.y,0u,specHit)', source)
        self.assertIn('imageStore(rrHitDistance,rrPixel,vec4(specDistance))', source)

    def test_bypasses_native_filters_without_losing_history_validity(self):
        source = (ROOT / 'code/renderer_vulkan/vk_pathtrace.c').read_text()
        self.assertIn('if (c[27] != 0 && !pt_rr.frame_ready)', source)
        self.assertIn('|| pt_rr.frame_ready;', source)
        self.assertIn('pt.temporal_valid = temporal_enabled;', source)
        self.assertIn('if (pt_rr.frame_ready != pt_rr.previous_active) pt.temporal_valid = qfalse;', source)

    def test_both_color_passes_cannot_run_in_one_frame(self):
        source = (ROOT / 'code/renderer_vulkan/vk_temporal.c').read_text()
        self.assertIn('evaluated = rr_evaluated || vk_sl_evaluate_dlss(&resources)', source)
        self.assertIn('evaluated && !rr_evaluated && temporal.sharpen_ready', source)

    def test_release_before_input_image_destruction(self):
        source = (ROOT / 'code/renderer_vulkan/vk_temporal.c').read_text()
        shutdown = source[source.index('void vk_temporal_shutdown'):source.index('qboolean vk_temporal_active')]
        self.assertLess(shutdown.index('vk_sl_release_frame_resources'), shutdown.index('vk_rt_shutdown'))
        source = (ROOT / 'code/renderer_vulkan/vk_streamline.cpp').read_text()
        shutdown = source[source.index('extern "C" void vk_sl_release_frame_resources'):source.index('extern "C" void vk_sl_shutdown')]
        self.assertLess(shutdown.index('freeResources(sl::kFeatureDLSS_RR'), shutdown.index('vk_dlssnr_shutdown'))

    def test_modes_and_all_required_guides(self):
        source = (ROOT / 'code/renderer_vulkan/vk_streamline.cpp').read_text()
        for tag in ('kBufferTypeAlbedo', 'kBufferTypeSpecularAlbedo', 'kBufferTypeNormalRoughness', 'kBufferTypeSpecularHitDistance'):
            self.assertIn(tag, source)
        self.assertIn('g_sl.rrOptions.mode = modes[dlssMode]', source)
        self.assertIn('g_sl.rrOptions.worldToCameraView = worldToCamera', source)
        self.assertIn('g_sl.rrOptions.preExposure = 1.0f', source)

    def test_optional_feature_and_build_wiring(self):
        source = (ROOT / 'code/renderer_vulkan/vk_streamline.cpp').read_text()
        self.assertIn('g_sl.rrSupported = g_sl.rrRequirementsAvailable &&', source)
        script = (SHADERS / 'compile-raytracing.ps1').read_text()
        make = (ROOT / 'Makefile').read_text()
        for name in ('pt_rr_guides', 'pt_rr_pack', 'pt_rr_post'):
            self.assertIn(f"'{name}'", script)
            self.assertIn(f'{name}_comp.o', make)
        for name in ('sl.dlss_d.dll', 'nvngx_dlssd.dll'):
            self.assertIn(name, make)

    def test_native_menu_control_and_bounds(self):
        menu = (ROOT / 'code/q3_ui/ui_video.c').read_text()
        self.assertIn('"DLSS Ray Reconstruction:"', menu)
        self.assertIn('s_ivo.dlssrr != s_graphicsoptions.dlssrr.curvalue', menu)
        self.assertIn('trap_Cvar_SetValue( "r_dlssRayReconstruction", s_graphicsoptions.dlssrr.curvalue )', menu)
        self.assertIn('Menu_AddItem( &s_graphicsoptions.menu, ( void * ) &s_graphicsoptions.dlssrr )', menu)
        section = menu[menu.index('y = 240 - 9 * (BIGCHAR_HEIGHT + 2);'):]
        y = 240 - 9 * 18
        rows = {}
        for line in section.splitlines():
            match = re.search(r's_graphicsoptions\.(\w+)\.generic\.y\s*= y;', line)
            if match: rows[match[1]] = y
            if 'y += 12;' in line: y += 12
            if 'y += BIGCHAR_HEIGHT+2;' in line: y += 18
            if 's_graphicsoptions.back.generic.type' in line: break
        self.assertIn('dlssrr', rows)
        self.assertTrue(all(48 <= value <= 480-16 for value in rows.values()), rows)


def read_run(directory):
    manifest = json.loads((directory / 'settings.json').read_text(encoding='utf-8-sig'))
    log = (directory / 'baseq3/qconsole.log').read_text(errors='replace')
    name = directory.name.removeprefix('performance-')
    summary = json.loads((directory.parent / f'pt-perf-{name}.run.json').read_text(encoding='utf-8-sig'))
    assert summary['sourceSettingsUnchanged'] and not summary['timedOut'], summary
    assert 'PT_PROGRAM_COMPLETE' in log
    assert 'Frame Generation: enabled 0, viewport active 0' in log
    assert manifest['settings']['r_dlssFrameGeneration'] == '0'
    assert manifest['settings']['r_dlssNeuralRendering'] == '0'
    assert manifest['settings']['r_dlss'] == '5', 'Comparison must use the requested DLAA mode'
    assert manifest['settings']['r_pathTracingBounces'] == '4'
    rr = manifest['settings']['r_dlssRayReconstruction'] == '1'
    if rr:
        assert 'Ray Reconstruction active: 1920x1080 -> 1920x1080' in log
        assert 'Ray Reconstruction: supported 1, enabled 1' in log
    samples = []
    active = False
    for line in log.splitlines():
        if line == 'PT_PROGRAM_REFERENCE_A': active = True
        if active and line.startswith('PT_PROFILE '):
            samples.append({k: float(v) for k, v in re.findall(r'(\w+)=([\d.]+)', line)})
    assert len(samples) >= 20, len(samples)
    samples = samples[3:-3]
    medians = {k: statistics.median(x[k] for x in samples) for k in samples[0]}
    if rr:
        assert medians['spatial'] < .005, 'Native spatial reconstruction still running'
    return manifest, {'run': name, 'samples': len(samples), 'rr': rr,
                      'spp': int(manifest['settings']['r_pathTracingSamples']),
                      'real_fps': 1000 / medians['frame'], 'medians_ms': medians}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runs', nargs='*', type=Path)
    parser.add_argument('--cc', help='Also compile and run real RR allocation/failure code against a mocked driver')
    parser.add_argument('--sdk', type=Path, help='Vulkan SDK root (for the resource fixture headers)')
    parser.add_argument('--validation', nargs='*', type=Path, help='Completed DLAA lifecycle/optics run directories')
    args = parser.parse_args()
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(RayReconstructionTests))
    if not result.wasSuccessful(): raise SystemExit(1)
    if args.cc:
        assert args.sdk, '--cc requires --sdk'
        with tempfile.TemporaryDirectory(prefix='pt-rr-resources-') as folder:
            exe = str(Path(folder) / 'rr-resource-check.exe')
            subprocess.run([args.cc, str(ROOT / 'tests/pt_rr_resource_fixture.c'), '-std=c11', '-O1',
                            '-I', str(args.sdk / 'Include'), '-o', exe], check=True, timeout=30)
            subprocess.run([exe], check=True, timeout=10)
    for directory in args.validation or []:
        name = directory.name.removeprefix('performance-')
        summary = json.loads((directory.parent / f'pt-perf-{name}.run.json').read_text(encoding='utf-8-sig'))
        manifest = json.loads((directory / 'settings.json').read_text(encoding='utf-8-sig'))
        log = (directory / 'baseq3/qconsole.log').read_text(errors='replace')
        assert summary['validationLayerLoaded'] and summary['sourceSettingsUnchanged']
        assert not summary['timedOut'] and summary['exitCode'] == 0 and summary['completionMarker']
        assert not (directory / 'validation.log').read_text().strip()
        # The layer file may be reopened on vid_restart. Its callbacks also
        # enter qconsole, which retains every renderer lifetime in the run.
        assert '[debugUtilsMessengerCallback]' not in log
        assert not re.search(r'VUID-|SYNC-HAZARD-|Validation (?:Error|Warning)', log)
        assert manifest['settings']['r_dlss'] == '5'
        assert manifest['settings']['r_dlssFrameGeneration'] == '0'
        assert manifest['settings']['r_dlssNeuralRendering'] == '0'
        assert 'Ray Reconstruction: supported 1, enabled 1' in log
        assert 'Ray Reconstruction evaluation failed' not in log
        markers = ('BEFORE_RESTART', 'AFTER_RESTART', 'NATIVE_DIAGNOSTIC', 'RESUMED') if 'lifecycle' in manifest['config'] else ('GLASS', 'MIRROR', 'MIRROR_MOVING')
        for marker in markers: assert 'PT_RR_' + marker in log
        print(f'PASS: {name}: DLAA RR, expected phases, validation clean, settings untouched')
    if args.runs:
        baseline = None
        outputs = []
        for path in args.runs:
            manifest, output = read_run(path)
            settings = {k: v for k, v in manifest['settings'].items()
                        if k not in ('r_dlssRayReconstruction', 'r_pathTracingSamples')}
            identity = (manifest['sourceSha256'], manifest['config'], settings)
            if baseline is not None: assert identity == baseline, 'Runs have mismatched settings or scenes'
            baseline = identity
            outputs.append(output)
        print(json.dumps(outputs, indent=2))
