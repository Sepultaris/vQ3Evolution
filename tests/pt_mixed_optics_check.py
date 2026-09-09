"""Offline checks for transmission-first paths containing mandatory reflection."""
from pathlib import Path
import unittest
import numpy as np
from pt_nested_transmission_check import Boundary, inverse_path, media_ratios, replay
from pt_transmission_math_check import unit


class MixedOptics(unittest.TestCase):
    def fixture(self):
        first = Boundary(np.zeros(3), np.array([0., 0, 1]), 1/1.5)
        tir = Boundary(np.array([0., 0, -20]), unit([.9, 0, .435]), 1.5,
                       entering=False, internal_reflection=True)
        camera = np.array([0., 0, 100])
        point = np.zeros(3)
        return camera, point, [first, tir]

    def check_path(self, camera, point, boundaries):
        ray = replay(point, camera, boundaries)
        self.assertIsNotNone(ray)
        start, outgoing = ray
        target = start+outgoing*50
        found = inverse_path(point+[.5, -.3, 0], camera, target, boundaries)
        self.assertIsNotNone(found)
        np.testing.assert_allclose(found, point, atol=.08)
        return target

    def test_transmission_reflection_target(self):
        camera, point, boundaries = self.fixture()
        target = self.check_path(camera, point, boundaries)
        self.assertGreater((target-boundaries[-1].point)@boundaries[-1].normal, 0)

    def test_transmission_reflection_transmission_target(self):
        camera, point, boundaries = self.fixture()
        start, outgoing = replay(point, camera, boundaries)
        boundaries.append(Boundary(start+outgoing*20, -outgoing, 1.5, entering=False))
        self.check_path(camera, point, boundaries)

    def test_tir_does_not_pop_medium(self):
        ratios, stack = media_ratios([(1.5, True, 0), (1.5, False, 0, True), (1.5, False, 0)])
        np.testing.assert_allclose(ratios, [1/1.5, 1.5, 1.5])
        self.assertEqual(stack, [])

    def test_previous_branch_switch_rejected(self):
        camera, point, boundaries = self.fixture()
        target = self.check_path(camera, point, boundaries)
        boundaries[1].normal = np.array([0., 0, 1])
        self.assertIsNone(replay(point, camera, boundaries))
        self.assertIsNone(inverse_path(point, camera, target, boundaries))

    def test_finite_tir_surface_still_required(self):
        camera, point, boundaries = self.fixture()
        target = self.check_path(camera, point, boundaries)
        boundaries[1].triangle = np.array([[100., 100, -20], [102, 100, -20], [100, 102, -20]])
        self.assertIsNone(inverse_path(point, camera, target, boundaries))

    def test_moving_target_after_internal_reflection(self):
        camera, point, boundaries = self.fixture()
        target = self.check_path(camera, point, boundaries)
        moved = inverse_path(point, camera, target+[0, 5, 0], boundaries)
        self.assertIsNotNone(moved)
        self.assertGreater(np.linalg.norm(moved-point), .5)

    def test_shader_keeps_first_lobe_and_branch_guards(self):
        root = Path(__file__).resolve().parents[1]/'code/renderer_vulkan/shaders'
        path = (root/'pt_transmission_path.glsl').read_text()
        geometry = (root/'pt_dielectric_geometry.glsl').read_text()
        self.assertIn('if(depth==0) return', path)
        self.assertIn('flags|=4u', path)
        self.assertIn('m.optical.y==0 && !internalReflection', path)
        self.assertIn('lastReflects ? targetSide<=pc.parameters.x:targetSide>=-pc.parameters.x', path)
        self.assertIn('dot(transmitted,transmitted)>=0.000001', geometry)
        self.assertIn('interfaceTotalInternalReflection(point,start', path)


if __name__ == '__main__':
    unittest.main()
