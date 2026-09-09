"""Offline wave-reflection inversion, compact storage and rejection checks.

These mirror the GLSL contracts; they do not execute shaders or validate GPU
history reuse, visual quality, frame time, or NVIDIA integration.
"""
import math
import unittest
from pathlib import Path
import numpy as np
from pt_transmission_math_check import BIAS, interface, solve, unit


def reflected_ray(point, camera, facing, time=0, entering=True, water=True):
    return interface(point, camera, facing, 1, 0, water, entering, time, reflection=True)


def reflection_point(seed, camera, target, facing, time=0, entering=True, water=True):
    return solve(seed, camera, target, facing, 1, water=water, entering=entering, time=time, reflection=True)


def pack_normal(normal):
    normal = unit(normal)
    normal /= np.abs(normal).sum()
    octahedral = normal[:2]
    if normal[2] < 0:
        octahedral = (1-np.abs(octahedral[::-1]))*np.where(octahedral >= 0, 1, -1)
    components = np.rint(np.clip(octahedral, -1, 1)*32767).astype(np.int32)
    bits = np.array([(int(components[0]) & 0xffff) | ((int(components[1]) & 0xffff) << 16)], dtype=np.uint32)
    return bits.view(np.float32)[0]


def unpack_normal(packed):
    bits = int(np.array([packed], dtype=np.float32).view(np.uint32)[0])
    signed = [(n if n < 32768 else n-65536) for n in (bits & 0xffff, bits >> 16)]
    octahedral = np.maximum(np.array(signed)/32767, -1)
    normal = np.array([*octahedral, 1-abs(octahedral[0])-abs(octahedral[1])])
    if normal[2] < 0:
        normal[:2] = (1-np.abs(octahedral[::-1]))*np.where(octahedral >= 0, 1, -1)
    return unit(normal)


def compatible_water_tap(old_point, old_target, *, kind=2, primary_id=7, target_id=9,
                         primary_normal=(0, 0, 1), target_normal=(0, 0, -1), valid=True):
    # Expected point=(0,0,0), target=(0,0,100), both footprints=1.
    primary, expected_normal = np.array([0., 0, 1]), np.array([0., 0, -1])
    old_point, old_target = np.asarray(old_point), np.asarray(old_target)
    if not valid or primary_id != 7 or np.dot(primary_normal, primary) < .95:
        return False
    if abs(old_point @ primary) > .1 or old_point @ old_point > 4:
        return False
    if kind != 2 or target_id != 9 or np.dot(target_normal, expected_normal) < .95:
        return False
    delta = old_target-np.array([0., 0, 100])
    return abs(delta @ expected_normal) <= .1 and delta @ delta <= 4


class ReflectionMath(unittest.TestCase):
    def roundtrip(self, camera, point, normal, time=0, entering=True):
        camera, point, normal = map(lambda x: np.array(x, dtype=float), (camera, point, normal))
        start, outgoing = reflected_ray(point, camera, normal, time, entering)
        target = start+outgoing*100
        seed = point+np.cross(normal, [.3, .4, .5])*2
        found = reflection_point(seed, camera, target, normal, time, entering)
        self.assertIsNotNone(found)
        np.testing.assert_allclose(found, point, atol=.08)
        start, outgoing = reflected_ray(found, camera, normal, time, entering)
        distance = (target-start) @ outgoing
        self.assertGreater(distance, 0)
        self.assertLess(np.linalg.norm(start+outgoing*distance-target), .08)

    def test_previous_wave_roundtrip(self):
        for time in (0, .25, 1, 3.75):
            for theta in (0, 20, 50, 70):
                self.roundtrip([100*math.tan(math.radians(theta)), -15, 100], [11, -20, 0], [0, 0, 1], time)

    def test_underwater_reflection_roundtrip(self):
        for time in (0, .25, 1, 3.75):
            self.roundtrip([5, 7, -100], [0, 0, 0], [0, 0, -1], time, entering=False)

    def test_moving_rotated_surface_roundtrip(self):
        normal, point = unit([.3, -.2, 1]), np.array([20., -70, 50])
        self.roundtrip(point+normal*100+np.cross(normal, [20, 10, 7]), point, normal, .75)

    def test_planar_limit_matches_analytic_mirror(self):
        camera, target, normal = np.array([15., 7, 100]), np.array([-30., 5, 80]), np.array([0., 0, 1])
        # The guide ray begins at point + normal*bias; remove that offset before
        # reflecting the target across the surface to get the analytic solution.
        virtual = target-normal*BIAS
        virtual -= 2*(virtual @ normal)*normal
        expected = camera+(virtual-camera)*(-camera[2]/(virtual[2]-camera[2]))
        found = reflection_point(np.zeros(3), camera, target, normal, water=False)
        np.testing.assert_allclose(found, expected, atol=.005)

    def test_previous_time_not_current_time(self):
        camera, point, normal = np.array([45., -15, 100]), np.array([11., -20, 0]), np.array([0., 0, 1])
        start, outgoing = reflected_ray(point, camera, normal)
        target = start+outgoing*80
        old = reflection_point(point, camera, target, normal)
        wrong = reflection_point(point, camera, target, normal, 1.5)
        np.testing.assert_allclose(old, point, atol=.005)
        self.assertIsNotNone(wrong)
        self.assertGreater(np.linalg.norm(wrong-old), 1)

    def test_moving_target_changes_projection(self):
        camera, normal = np.array([0., 0, 100]), np.array([0., 0, 1])
        old = reflection_point(np.zeros(3), camera, np.array([-30., 0, 100]), normal)
        new = reflection_point(np.zeros(3), camera, np.array([30., 0, 100]), normal)
        self.assertIsNotNone(old); self.assertIsNotNone(new)
        self.assertGreater(np.linalg.norm(new-old), 20)

    def test_real_target_behind_camera_projects_interface(self):
        camera, normal = np.array([0., 0, 100]), np.array([0., 0, 1])
        target = np.array([0., 0, 200])
        point = reflection_point(np.zeros(3), camera, target, normal)
        self.assertIsNotNone(point)
        self.assertLess((target-camera) @ -normal, 0)  # Projecting the real target is wrong.
        self.assertGreater((point-camera) @ -normal, 0)

    def test_wrong_side_and_grazing_rejected(self):
        camera, normal, seed = np.array([0., 0, 100]), np.array([0., 0, 1]), np.zeros(3)
        for target in (np.array([0., 0, -100]), np.array([0., 0, BIAS])):
            self.assertIsNone(reflection_point(seed, camera, target, normal))
        self.assertIsNone(reflection_point(seed, -camera, camera, normal))
        self.assertIsNone(reflected_ray(seed, np.array([10000., 0, 1]), normal))

    def test_grazing_wave_matches_radiance_fallback(self):
        point, camera, facing = np.zeros(3), np.array([100., 0, 1]), np.array([0., 0, 1])
        # Wave normal points away from this grazing view. The radiance shader
        # falls back to geometric, NOT an inverted wave normal beneath the plane.
        start, outgoing = reflected_ray(point, camera, facing)
        incident = unit(point-camera)
        np.testing.assert_allclose(outgoing, incident-2*(incident @ facing)*facing)
        np.testing.assert_allclose(start, facing*BIAS)

    def test_compact_normal_precision_and_record_size(self):
        rng = np.random.default_rng(37)
        normals = np.vstack((np.eye(3), -np.eye(3), rng.normal(size=(4096, 3))))
        for normal in normals:
            restored = unpack_normal(pack_normal(normal))
            self.assertLess(np.linalg.norm(restored-unit(normal)), .00015)
        record = np.zeros(8, dtype=np.float32)
        record[:4], record[4:7], record[7] = [10, 20, 30, 1], [4, 5, 6], pack_normal([.5, -.3, -1])
        self.assertEqual(record.nbytes, 32)
        np.testing.assert_allclose(unpack_normal(record[7]), unit([.5, -.3, -1]), atol=.00015)

    def test_old_tap_rejection(self):
        point, target = [0., 0, 0], [0., 0, 100]
        self.assertTrue(compatible_water_tap(point, target))
        for mismatch in ({"kind": 1}, {"kind": -1}, {"kind": 0}, {"primary_id": 8},
                         {"target_id": 8}, {"primary_normal": (0, 1, 0)},
                         {"target_normal": (0, 0, 1)}, {"valid": False}):
            self.assertFalse(compatible_water_tap(point, target, **mismatch))
        for wrong_point in ([0., 0, .11], [3., 0, 0]):
            self.assertFalse(compatible_water_tap(wrong_point, target))
        for wrong_target in ([0., 0, 100.11], [3., 0, 100]):
            self.assertFalse(compatible_water_tap(point, wrong_target))

    def test_native_contract_no_new_buffer(self):
        root = Path(__file__).resolve().parents[1]/"code/renderer_vulkan"
        native = (root/"vk_pathtrace.c").read_text()
        integrator = (root/"shaders/pt_integrator.glsl").read_text()
        temporal = (root/"shaders/pt_temporal.comp").read_text()
        helpers = (root/"shaders/pt_guide_helpers.glsl").read_text()
        self.assertIn("bindings[48]", native)
        self.assertIn("VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 41", native)
        self.assertIn("reflected.position.w=-1", integrator)
        self.assertIn("reflected.position.w=2", integrator)
        self.assertIn("previousReflectionPoint(oldPosition.xyz,oldNormal.xyz,oldTarget.position.xyz", integrator)
        self.assertIn("vec4(previousPoint,packReflectionNormal(oldTarget.normal.xyz))", integrator)
        self.assertIn("reflection,rp.depthProjection.w", helpers)
        self.assertIn("water ? expected.normal.xyz:expected.position.xyz", temporal)
        self.assertIn("length(expected.position.xyz-expected.normal.xyz)", temporal)
        self.assertIn("if(reflected.position.w<0) projected=false", temporal)
        self.assertIn("old.position.w!=2", temporal)
        self.assertIn("water ? 4:", temporal)
        for source in (integrator, temporal):
            self.assertIn("struct ReflectionGuide { vec4 position; vec4 normal; };", source)


if __name__ == "__main__":
    unittest.main()
