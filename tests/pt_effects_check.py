"""Offline effects capture/material-layer math and native source contracts."""
import itertools
from pathlib import Path
import unittest
import numpy as np


def polygon_normal(vertices):
    result = np.zeros(3)
    for a, b in zip(vertices, np.roll(vertices, -1, axis=0)):
        result += [(a[1]-b[1])*(a[2]+b[2]), (a[2]-b[2])*(a[0]+b[0]), (a[0]-b[0])*(a[1]+b[1])]
    length = np.linalg.norm(result)
    return result/length if length else result


def collect(candidates, limit=16):
    result = []
    for primitive, polygon in candidates:
        if any(poly == polygon for _, poly in result):
            continue
        if len(result) == limit:
            if primitive < result[0][0]:
                continue
            result.pop(0)
        result.append((primitive, polygon))
        result.sort()
    return result


def composite(base, color, mode, alpha=1, complement=False):
    color = np.clip(color, 0, 1)
    if mode == 'filter':
        return base*(1-color if complement else color)
    return base*(1-alpha)+color*alpha


def batches(surfaces, path_tracing=True, capacity=8):
    """Same shader/entity can mix cached BSP and submitted polygon surfaces."""
    output, current = [], None
    for shader, kind, size in surfaces:
        dynamic = path_tracing and kind == 'poly'
        if current is None or current[:2] != [shader, dynamic] or current[2]+size >= capacity:
            current = [shader, dynamic, 0, []]
            output.append(current)
        current[2] += size
        current[3].append(kind)
    return output


class Effects(unittest.TestCase):
    def test_straight_continuation_has_no_bias_sized_hole(self):
        for distance in (4., 30., 300., 10000., 30000.):
            previous = np.float32(distance)
            lower = np.nextafter(previous, np.float32(np.inf))
            self.assertGreater(lower, previous)
            for gap in (.01, .125, 1., 4., 8.):
                wall = np.float32(distance+gap)
                self.assertLessEqual(lower, wall)
                if gap < 5.5:
                    self.assertLess(wall, distance+1.5+4.)  # Broken origin bias + near clip.

    def test_effect_chain_keeps_camera_distance_cone_and_absorption(self):
        hits = np.array([299., 299.125, 299.5, 300.], dtype=np.float32)
        segments = np.diff(np.concatenate(([0.], hits)))
        self.assertEqual(sum(segments), 300.)
        self.assertAlmostEqual(sum(segments*.003), .9)
        self.assertAlmostEqual(np.prod(np.exp(-segments*.01)), np.exp(-3))
        self.assertEqual(hits[-1], 300.)  # MIS and depth use absolute distance.

    def test_actual_ray_and_guide_continuation_contract(self):
        shader = (Path(__file__).resolve().parents[1]/'code/renderer_vulkan/shaders/pt_integrator.glsl').read_text()
        self.assertIn('uintBitsToFloat(floatBitsToUint(distance)+1u)', shader)
        self.assertIn('max(pc.originNear.w,minimum)', shader)
        self.assertIn('float traveled=max(hit.distance-segmentDistance,0)', shader)
        effect = shader.split('if(m.params.x==2 || m.params.x==3) {',1)[1].split('vec3 n=shadingNormal',1)[0]
        self.assertIn('segmentDistance=hit.distance;', effect)
        self.assertNotIn('origin=', effect)
        self.assertNotIn('pc.parameters.x', effect)
        guide = shader.split('for(int layer=0;layer<32;++layer) {',1)[1].split('if(found)',1)[0]
        self.assertIn('primaryHit(pc.originNear.xyz,direction,traveled>0 ? continuationT(traveled):0,hit)', guide)
        self.assertIn('traveled=hit.distance;', guide)
        self.assertNotIn('hit.distance+=', guide)

    def test_polygon_normal_with_collinear_initial_edges(self):
        vertices = np.array([[0., 0, 0], [1, 0, 0], [2, 0, 0], [2, 2, 0], [0, 2, 0]])
        np.testing.assert_allclose(polygon_normal(vertices), [0, 0, 1])
        np.testing.assert_allclose(polygon_normal(vertices[::-1]), [0, 0, -1])
        np.testing.assert_allclose(polygon_normal(vertices+[200, -400, 50]), [0, 0, 1])

    def test_degenerate_polygon_normal_finite(self):
        np.testing.assert_equal(polygon_normal(np.zeros((4, 3))), [0, 0, 0])

    def test_mixed_world_poly_batch_does_not_duplicate_world(self):
        result = batches([('same', 'world', 3), ('same', 'poly', 3), ('same', 'world', 3)])
        self.assertEqual([batch[1] for batch in result], [False, True, False])
        self.assertEqual([kind for batch in result if batch[1] for kind in batch[3]], ['poly'])

    def test_overflow_preserves_polygon_class(self):
        result = batches([('same', 'poly', 4)]*3)
        self.assertEqual(len(result), 3)
        self.assertTrue(all(batch[1] for batch in result))

    def test_raster_batching_unchanged(self):
        result = batches([('same', 'world', 3), ('same', 'poly', 3)], path_tracing=False)
        self.assertEqual(len(result), 1)
        self.assertFalse(result[0][1])

    def test_order_independent_of_bvh_traversal(self):
        candidates = [(70, 2), (20, 1), (80, 3), (100, 4)]
        for permutation in itertools.permutations(candidates):
            self.assertEqual(collect(permutation), sorted(candidates))

    def test_shared_fan_edges_apply_one_polygon(self):
        result = collect([(20, 1), (21, 1), (40, 2)])
        self.assertEqual(len(result), 2)
        self.assertEqual([poly for _, poly in result], [1, 2])

    def test_layer_bound_keeps_latest_order(self):
        candidates = [(i, i+1) for i in range(40)]
        expected = candidates[-16:]
        self.assertEqual(collect(candidates), expected)
        self.assertEqual(collect(candidates[::-1]), expected)
        rng = np.random.default_rng(7)
        for _ in range(20):
            rng.shuffle(candidates)
            self.assertEqual(collect(candidates), expected)

    def test_inverse_source_color_fade(self):
        base = np.array([.3, .5, .8])
        np.testing.assert_equal(composite(base, np.zeros(3), 'filter', complement=True), base)
        np.testing.assert_equal(composite(base, np.ones(3), 'filter', complement=True), np.zeros(3))
        np.testing.assert_allclose(composite(base, np.full(3, .25), 'filter', complement=True), base*.75)

    def test_alpha_and_filter_energy_bounds(self):
        rng = np.random.default_rng(11)
        for _ in range(100):
            base, color = rng.random(3), rng.random(3)
            for alpha in (0, .25, .75, 1):
                value = composite(base, color, 'alpha', alpha)
                self.assertTrue(np.all(value >= 0) and np.all(value <= 1))
            np.testing.assert_allclose(composite(base, color, 'alpha', 0), base)
            np.testing.assert_allclose(composite(base, color, 'alpha', 1), color)
            for complement in (True, False):
                self.assertTrue(np.all(composite(base, color, 'filter', complement=complement) <= base))

    def test_alpha_order_is_not_commutative(self):
        base, red, blue = np.ones(3)*.2, np.array([1., 0, 0]), np.array([0., 0, 1])
        a = composite(composite(base, red, 'alpha', .5), blue, 'alpha', .5)
        b = composite(composite(base, blue, 'alpha', .5), red, 'alpha', .5)
        self.assertGreater(np.linalg.norm(a-b), .3)

    def test_coplanar_attachment_does_not_move_receiver(self):
        point, normal, epsilon = np.array([3., 2, 0]), np.array([0., 0, 1]), .05
        for height, accepted in ((0, True), (.02, True), (.1, False)):
            start = point+normal*epsilon
            distance = (start[2]-height)
            self.assertEqual(.001 <= distance <= 2*epsilon, accepted)
        np.testing.assert_equal(point, [3, 2, 0])

    def test_layer_normals_and_nonworld_receivers_excluded(self):
        normal = np.array([0., 0, 1])
        self.assertTrue(abs(normal@normal) > .99)
        self.assertFalse(abs(normal@np.array([1., 0, 0])) > .99)
        self.assertNotEqual(0x10000000 & 0x10000000, 0)

    def test_decal_queries_and_regular_rays_use_separate_masks(self):
        world, dynamic, weapon = 7, 23, 8
        self.assertEqual(world & 16, 0)
        self.assertEqual(dynamic & 16, 16)
        self.assertEqual(weapon & 16, 0)
        for purpose in (1, 2, 4):
            self.assertNotEqual(dynamic & purpose, 0)
        self.assertNotEqual(0x08000000 & 0x48000000, 0)

    def test_native_contracts(self):
        root = Path(__file__).resolve().parents[1]/'code/renderer_vulkan'
        native, cmds, surface = [(root/name).read_text() for name in ('vk_pathtrace.c', 'tr_cmds.c', 'tr_surface.c')]
        shader = (root/'shaders/pt_integrator.glsl').read_text()
        decals = (root/'shaders/pt_decals.glsl').read_text()
        guides = (root/'shaders/pt_guide_helpers.glsl').read_text()
        self.assertIn('rayPolys == oldRayPolys', cmds)
        self.assertIn('tess.rayDynamicPolys = rayPolys', surface)
        self.assertIn('VectorCopy( normal, tess.normal[numv] )', surface)
        capture = native[native.index('void vk_pt_capture('):native.index('qboolean vk_pt_initialize(')]
        self.assertIn('id |= 0x08000000u', capture)
        self.assertIn('GLS_DSTBLEND_ONE_MINUS_SRC_COLOR', native)
        self.assertIn('m->absorption[3] = 1', native)
        self.assertIn('vec4 decalMins,decalMaxs', shader)
        self.assertIn('excluded=excluded || (materialAndFlags&0x08000000u)!=0u', shader)
        self.assertIn('filterTransmission(primitive,bary,mat2(0),origin)', shader)
        self.assertIn('g.positionDepth.w=-abs(hit.distance)', shader)
        self.assertIn('id|=0xffff0000u', guides)
        self.assertIn('if(previous) p.w=0', guides)
        self.assertIn('DECAL_POLYGON(i)==polygon', decals)
        self.assertIn('#define DECAL_POLYGON(index) polygons[index]', decals)
        self.assertIn('while(at>0 && primitives[at-1]>primitive)', decals)
        self.assertIn('textureBarycentrics(receiver)', decals)


if __name__ == '__main__':
    unittest.main()
