"""Deterministic camera mapping/disocclusion tests for pt_temporal.comp math."""
import math
import unittest


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def project(point, origin, forward, right, up, scale, jitter, size):
    delta = [a - b for a, b in zip(point, origin)]
    depth = dot(delta, forward)
    if depth <= 0.001:
        return None
    ndc = [dot(delta, axis) * s / depth - j
           for axis, s, j in zip((right, up), scale, jitter)]
    return [(n * 0.5 + 0.5) * extent - 0.5 for n, extent in zip(ndc, size)]


def compatible(previous, world, old_normal, normal, old_material, material, footprint):
    separation = [a - b for a, b in zip(previous, world)]
    return (old_material == material and dot(old_normal, normal) >= 0.95
            and abs(dot(separation, normal)) <= max(0.03, footprint * 0.1)
            and dot(separation, separation) <= footprint * footprint * 4)


class ReprojectionTests(unittest.TestCase):
    def test_virtual_mirror_target_matches_reflection_law_and_motion(self):
        plane, n = [300, 0, 0], [-1, 0, 0]
        def mirror(p):
            distance = dot([a-b for a,b in zip(p,plane)], n)
            return [a-2*distance*b for a,b in zip(p,n)]
        for size in ((640, 360), (3440, 1440)):
            camera = ([0, 0, 0], [1, 0, 0], [0, -1, 0], [0, 0, 1], [1, -size[0]/size[1]], [0, 0], size)
            target, previous = [-100, 40, 20], [-100, 15, 25]
            virtual, old_virtual = mirror(target), mirror(previous)
            self.assertEqual(virtual, [700, 40, 20])
            hit = [p*300/virtual[0] for p in virtual]
            incoming = [p/math.sqrt(dot(hit,hit)) for p in hit]
            reflected = [a-2*dot(incoming,n)*b for a,b in zip(incoming,n)]
            to_target = [a-b for a,b in zip(target,hit)]
            for a,b in zip(reflected,to_target):
                self.assertAlmostEqual(a,b/math.sqrt(dot(to_target,to_target)))
            new_pixel, old_pixel = project(virtual,*camera), project(old_virtual,*camera)
            self.assertGreater(abs(new_pixel[0]-old_pixel[0]), 10)
            # A stationary mirror-interface vector is zero for a fixed camera;
            # it cannot represent this moving reflection.
            self.assertNotEqual(new_pixel,old_pixel)
            np = project(mirror(mirror(target)),*camera)
            self.assertIsNone(np)  # Real object is behind the camera.

    def test_deforming_triangle_uses_previous_barycentric_position(self):
        old = [[100, -10, 0], [100, 10, 0], [100, 0, 20]]
        # Translation plus a non-rigid pose change cannot be represented by
        # camera-only motion or a single object transform.
        new = [[105, 0, 0], [104, 20, 2], [107, 9, 25]]
        weights = [0.2, 0.3, 0.5]
        old_hit = [sum(old[v][c] * weights[v] for v in range(3)) for c in range(3)]
        new_hit = [sum(new[v][c] * weights[v] for v in range(3)) for c in range(3)]
        camera = ([0, 0, 0], [1, 0, 0], [0, 1, 0], [0, 0, 1], [1, 1], [0, 0], [640, 480])
        old_pixel = project(old_hit, *camera)
        new_pixel = project(new_hit, *camera)
        motion_uv = [(old_pixel[i] - new_pixel[i]) / camera[-1][i] for i in range(2)]
        for i in range(2):
            self.assertAlmostEqual(new_pixel[i] + motion_uv[i] * camera[-1][i], old_pixel[i])
        self.assertGreater(abs(motion_uv[0]), 0.04)

    def test_same_material_on_different_objects_cannot_share_history(self):
        normal = [0, 0, 1]
        material = 7
        self.assertFalse(compatible([0, 0, 0], [0, 0, 0], normal, normal,
                                    material | (33 << 16), material | (65 << 16), 1))

    def test_projection_roundtrip_with_jitter_and_widescreen(self):
        origin = [37, -80, 21]
        angle = math.radians(37)
        forward = [math.cos(angle), math.sin(angle), 0]
        right = [math.sin(angle), -math.cos(angle), 0]
        up = [0, 0, 1]
        for size in ((640, 480), (1920, 1080), (3440, 1440), (5120, 1440)):
            scale = (0.9, -0.9 * size[0] / size[1])
            for jitter in ((0, 0), (0.37 / size[0], -0.41 / size[1])):
                for pixel in ((0, 0), (size[0] - 1, size[1] - 1), (127.2, 211.8)):
                    ndc = [(p + 0.5) / extent * 2 - 1 for p, extent in zip(pixel, size)]
                    ray = [forward[k] + right[k] * (ndc[0] + jitter[0]) / scale[0]
                           + up[k] * (ndc[1] + jitter[1]) / scale[1] for k in range(3)]
                    point = [origin[k] + ray[k] * 500 for k in range(3)]
                    result = project(point, origin, forward, right, up, scale, jitter, size)
                    for a, b in zip(result, pixel):
                        self.assertAlmostEqual(a, b, places=9)

    def test_translation_uses_previous_camera(self):
        result = project([100, 10, 0], [0, 0, 0], [1, 0, 0], [0, 1, 0],
                         [0, 0, 1], [1, 1], [0, 0], [640, 480])
        self.assertAlmostEqual(result[0], 351.5)
        self.assertIsNone(project([-1, 0, 0], [0, 0, 0], [1, 0, 0], [0, 1, 0],
                                  [0, 0, 1], [1, 1], [0, 0], [640, 480]))

    def test_disocclusion_material_and_normal_rejection(self):
        n = [0, 0, 1]
        self.assertTrue(compatible([0.4, 0.2, 0], [0, 0, 0], n, n, 5, 5, 1))
        self.assertFalse(compatible([0, 0, 2], [0, 0, 0], n, n, 5, 5, 1))
        self.assertFalse(compatible([100, 0, 0], [0, 0, 0], n, n, 5, 5, 1))
        self.assertFalse(compatible([0, 0, 0], [0, 0, 0], n, n, 65541, 5, 1))
        self.assertFalse(compatible([0, 0, 0], [0, 0, 0], [0, 1, 0], n, 5, 5, 1))


if __name__ == "__main__":
    unittest.main()
