"""CPU math/invariant checks; no game launch or claim of GPU image validation."""
import math
import unittest
import numpy as np


def bary_gradients(e1, e2, ray, width):
    e1, e2, ray = map(lambda x: np.array(x, dtype=float), (e1, e2, ray))
    area_normal = np.cross(e1, e2)
    area = np.linalg.norm(area_normal)
    if area < .000001 or width <= 0:
        return np.zeros((2, 2))
    normal = area_normal / area
    x = np.cross([0, 0, 1] if abs(ray[2]) < .999 else [0, 1, 0], ray)
    x /= np.linalg.norm(x)
    y = np.cross(ray, x)
    cosine = normal @ ray
    cosine = (-1 if cosine < 0 else 1) * max(abs(cosine), .001)
    axes = width * (np.column_stack((x, y)) - np.outer(ray, normal @ np.column_stack((x, y))) / cosine)
    dual = np.vstack((np.cross(e2, normal), np.cross(normal, e1))) / area
    return dual @ axes


def mirror(point, plane, normal):
    point, plane, normal = map(np.asarray, (point, plane, normal))
    return point - 2 * ((point - plane) @ normal) * normal


class FilteringMath(unittest.TestCase):
    def test_frontal_texel_footprint(self):
        # One world unit per pixel on a 64-unit/one-repeat surface.
        gradient = bary_gradients([64, 0, 0], [0, 64, 0], [0, 0, -1], 1)
        np.testing.assert_allclose(np.linalg.svd(gradient, compute_uv=False), [1/64, 1/64])
        self.assertAlmostEqual(math.log2(np.linalg.norm(gradient[:, 0]) * 256), 2)

    def test_grazing_preserves_anisotropic_axis(self):
        for angle in (0, 30, 60, 80, 89):
            a = math.radians(angle)
            ray = [math.sin(a), 0, -math.cos(a)]
            singular = np.linalg.svd(bary_gradients([1, 0, 0], [0, 1, 0], ray, 1), compute_uv=False)
            np.testing.assert_allclose(singular, [1/math.cos(a), 1], rtol=1e-10)

    def test_degenerate_and_grazing_are_finite(self):
        for ray in ([1, 0, 0], [0, 0, -1]):
            gradient = bary_gradients([1, 0, 0], [2, 0, 0], ray, 1)
            np.testing.assert_array_equal(gradient, np.zeros((2, 2)))
            self.assertTrue(np.isfinite(bary_gradients([1, 0, 0], [0, 1, 0], ray, 1)).all())

    def test_distance_and_resolution_lod(self):
        gradient = bary_gradients([64, 0, 0], [0, 64, 0], [0, 0, -1], 1)
        twice_distance = bary_gradients([64, 0, 0], [0, 64, 0], [0, 0, -1], 2)
        np.testing.assert_allclose(twice_distance, 2*gradient)
        # Doubling render resolution halves cone spread and the footprint.
        twice_resolution = bary_gradients([64, 0, 0], [0, 64, 0], [0, 0, -1], .5)
        np.testing.assert_allclose(twice_resolution, .5*gradient)

    def test_skewed_mirrored_uv_projection(self):
        e1, e2 = np.array([10., 2, 1]), np.array([-1., 8, 3])
        ray = np.array([.2, .3, -1]); ray /= np.linalg.norm(ray)
        gradient = bary_gradients(e1, e2, ray, .2)
        normal = np.cross(e1, e2); normal /= np.linalg.norm(normal)
        x = np.cross([0, 0, 1], ray); x /= np.linalg.norm(x)
        axes = np.column_stack((x, np.cross(ray, x)))
        projected = .2*(axes - np.outer(ray, normal @ axes)/(normal @ ray))
        # Reconstruct both tangent-plane axes independently of the dual formula.
        np.testing.assert_allclose(np.column_stack((e1, e2)) @ gradient, projected, atol=1e-12)
        uv = np.array([[-2., .3], [.5, 1.]])
        np.testing.assert_allclose(uv @ gradient, uv @ np.linalg.lstsq(np.column_stack((e1, e2)), projected, rcond=None)[0])

    def test_ordered_uv_transform_jacobian(self):
        transform = np.array([[2., -.3], [.7, -.5]])
        angle = .8
        rotation = np.array([[math.cos(angle), -math.sin(angle)], [math.sin(angle), math.cos(angle)]])
        scale = np.diag([3., .4])
        uv = np.array([.17, .91]); delta = np.array([.03, -.02]); epsilon = 1e-5
        def apply(p):
            return rotation @ ((scale @ (transform @ p + [.1, -.7]) - .5) / 1.3) + .5
        analytical = rotation @ (scale @ transform @ delta) / 1.3
        np.testing.assert_allclose((apply(uv+epsilon*delta)-apply(uv))/epsilon, analytical, rtol=1e-8)

    def test_smooth_specular_fast_path_preserves_center(self):
        # Old filter rejects every non-center tap below its roughness limit.
        # Returning the center directly removes work, not a filtering pass.
        for center in ([0, 0, 0], [1, 4, .1], [100, .1, 13]):
            for weight in (.001, 36, 500):
                np.testing.assert_allclose(np.array(center)*weight/weight, center)

    def test_moving_mirror_uses_previous_plane(self):
        previous_target = np.array([4., 2, 3])
        previous_plane = np.array([0., 0, 0]); previous_normal = np.array([1., 0, 0])
        angle = math.radians(20)
        rotation = np.array([[math.cos(angle), -math.sin(angle), 0], [math.sin(angle), math.cos(angle), 0], [0, 0, 1]])
        translation = np.array([1., -2, .5])
        current_target = rotation @ previous_target + translation
        current_plane = rotation @ previous_plane + translation
        current_normal = rotation @ previous_normal
        old_virtual = mirror(previous_target, previous_plane, previous_normal)
        new_virtual = mirror(current_target, current_plane, current_normal)
        np.testing.assert_allclose(new_virtual, rotation @ old_virtual + translation)
        self.assertGreater(np.linalg.norm(old_virtual-mirror(previous_target, current_plane, current_normal)), 1)


if __name__ == "__main__":
    unittest.main()
