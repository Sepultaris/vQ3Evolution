"""Offline model/contract checks for bounded multi-interface reconstruction.

This models the shader math; it is not a GPU or game test.
"""
from dataclasses import dataclass, replace
from pathlib import Path
import unittest
import numpy as np
from pt_transmission_math_check import BIAS, interface, refract, solve, unit
from pt_reflection_math_check import pack_normal, unpack_normal


@dataclass
class Boundary:
    point: np.ndarray
    normal: np.ndarray
    eta: float
    thickness: float = 0
    water: bool = False
    entering: bool = True
    triangle: object = None
    tracked: bool = True
    internal_reflection: bool = False


def contains(triangle, point):
    a, b, c = np.asarray(triangle, dtype=float)
    u, v, w = b-a, c-a, point-a
    uu, uv, vv, wu, wv = u@u, u@v, v@v, w@u, w@v
    determinant = uu*vv-uv*uv
    if determinant <= .000001:
        return False
    bary = np.array([vv*wu-uv*wv, uu*wv-uv*wu])/determinant
    return bool(np.all(bary >= -.001) and bary.sum() <= 1.001)


def replay(point, camera, boundaries, time=0, finite=False):
    start, outgoing = camera.copy(), unit(point-camera)
    for i, boundary in enumerate(boundaries):
        hit = point.copy()
        if i:
            denominator = outgoing @ boundary.normal
            if denominator >= -.001:
                return None
            distance = ((boundary.point-start) @ boundary.normal)/denominator
            if distance <= .001:
                return None
            hit = start+outgoing*distance
        if finite and boundary.triangle is not None and not contains(boundary.triangle, hit):
            return None
        if boundary.internal_reflection:
            # Current fixture normals are flat; the production helper also
            # reconstructs previous procedural waves before this branch test.
            normal = boundary.normal
            if boundary.water:
                normal = normal.copy() if boundary.entering else -normal
                slope = np.array([.06*np.cos(hit[0]*.07+time*1.3), .045*np.cos(hit[1]*.09-time*1.1), 0])
                normal = unit(normal-slope+normal*(normal@slope))
                if normal @ boundary.normal < 0: normal = -normal
                if normal @ -outgoing < .001: normal = boundary.normal
            if refract(outgoing, normal, boundary.eta) is not None:
                return None
        ray = interface(hit, start, boundary.normal, boundary.eta, boundary.thickness,
                        boundary.water, boundary.entering, time, reflection=boundary.internal_reflection)
        if ray is None:
            return None
        start, outgoing = ray
    return start, outgoing


def inverse_path(seed, camera, target, boundaries, time=0, height=720, projection=1):
    if not boundaries or len(boundaries) > 4 or not all(b.tracked for b in boundaries):
        return None
    point = seed.copy()
    if len(boundaries) == 1:
        b = boundaries[0]
        return solve(point, camera, target, b.normal, b.eta, b.thickness, b.water, b.entering, time, height, projection)
    facing, last = boundaries[0].normal, boundaries[-1]
    side = (target-last.point) @ last.normal
    if (camera-point) @ facing <= .001 or (side <= BIAS if last.internal_reflection else side >= -BIAS):
        return None
    def basis(normal):
        u = unit(np.cross([0, 0, 1] if abs(normal[2]) < .999 else [0, 1, 0], normal))
        return u, np.cross(normal, u)
    u, v = basis(facing)
    end_u, end_v = basis(last.normal)
    path_distance = np.linalg.norm(point-camera)+np.linalg.norm(target-last.point)
    path_distance += sum(np.linalg.norm(b.point-a.point) for a, b in zip(boundaries, boundaries[1:]))
    footprint = max(.02, path_distance*2/(height*abs(projection)))
    epsilon, tolerance = max(.01, footprint*.05), max(.005, footprint*.1)
    def residual(p):
        ray = replay(p, camera, boundaries, time)
        if ray is None:
            return None
        start, outgoing = ray
        distance = ((target-start) @ last.normal)/(outgoing @ last.normal)
        if distance <= 0:
            return None
        error = start+outgoing*distance-target
        r = np.array([error@end_u, error@end_v])
        return r if np.isfinite(r).all() else None
    for _ in range(8):
        r = residual(point)
        if r is None:
            return None
        if np.linalg.norm(r) <= tolerance:
            break
        rx, ry = residual(point+u*epsilon), residual(point+v*epsilon)
        if rx is None or ry is None:
            return None
        jacobian = np.column_stack(((rx-r)/epsilon, (ry-r)/epsilon))
        if abs(np.linalg.det(jacobian)) < .0001:
            return None
        step = np.linalg.solve(jacobian, r)
        if not np.isfinite(step).all():
            return None
        step *= min(1, 128/max(np.linalg.norm(step), .0001))
        point -= u*step[0]+v*step[1]
    r = residual(point)
    if r is None or np.linalg.norm(r) > tolerance or replay(point, camera, boundaries, time, finite=True) is None:
        return None
    return point


def media_ratios(events, camera_ior=1):
    """The same four-medium stack and pane semantics as the radiance integrator."""
    stack = [camera_ior] if camera_ior > 1 else []
    ratios = []
    for event in events:
        ior, entering, thickness = event[:3]
        reflecting = len(event) > 3 and event[3]
        eta_i = stack[-1] if stack else 1
        eta_t = ior if entering else (stack[-2] if len(stack) > 1 else 1)
        if not entering and not stack and thickness == 0:
            eta_i = ior
            stack = [ior]
        if thickness > 0:
            eta_t = ior
        ratios.append(eta_i/eta_t)
        if thickness == 0 and not reflecting:
            if entering:
                if len(stack) >= 4:
                    return None
                stack.append(eta_t)
            elif stack:
                stack.pop()
    return ratios, stack


def signature(words):
    x, y = 2166136261, 2654435769
    for word in words:
        x = ((x ^ word)*16777619) & 0xffffffff
        y = ((y+word)*2246822519) & 0xffffffff
        y = ((y << 13) | (y >> 19)) & 0xffffffff
    return x, y


class NestedTransmission(unittest.TestCase):
    def boundaries(self, ratios, **kwargs):
        return [Boundary(np.array([0., 0, -20*i]), np.array([0., 0, 1]), eta, **kwargs)
                for i, eta in enumerate(ratios)]

    def roundtrip(self, boundaries, camera=None, time=0):
        camera = np.array([25., 7, 100]) if camera is None else camera
        point = boundaries[0].point.copy()
        ray = replay(point, camera, boundaries, time)
        self.assertIsNotNone(ray)
        start, outgoing = ray
        target = start+outgoing*80
        seed = point+np.cross(boundaries[0].normal, [.3, .4, .5])*2
        found = inverse_path(seed, camera, target, boundaries, time)
        self.assertIsNotNone(found)
        np.testing.assert_allclose(found, point, atol=.08)
        actual_start, actual_out = replay(found, camera, boundaries, time, finite=True)
        self.assertLess(np.linalg.norm(np.cross(target-actual_start, actual_out)), .1)
        return camera, point, target

    def test_nested_volume_ior_stack(self):
        events = [(1.5, True, 0), (1.333, True, 0), (1.333, False, 0), (1.5, False, 0)]
        ratios, remaining = media_ratios(events)
        np.testing.assert_allclose(ratios, [1/1.5, 1.5/1.333, 1.333/1.5, 1.5])
        self.assertEqual(remaining, [])
        boundaries = self.boundaries(ratios)
        boundaries[2].entering = boundaries[3].entering = False
        self.roundtrip(boundaries)

    def test_two_thin_panes(self):
        self.roundtrip(self.boundaries([1/1.5, 1/1.6], thickness=4))

    def test_pane_does_not_modify_enclosing_medium(self):
        ratios, remaining = media_ratios([(1.5, True, 4), (1.333, False, 0)], camera_ior=1.333)
        np.testing.assert_allclose(ratios, [1.333/1.5, 1.333])
        self.assertEqual(remaining, [])

    def test_backface_camera_initialization(self):
        ratios, remaining = media_ratios([(1.5, False, 0), (1.333, True, 0)])
        np.testing.assert_allclose(ratios, [1.5, 1/1.333])
        self.assertEqual(remaining, [1.333])

    def test_medium_overflow_rejected(self):
        self.assertIsNone(media_ratios([(1.5, True, 0)]*5))

    def test_wave_at_intermediate_interface(self):
        boundaries = self.boundaries([1/1.5, 1/1.333, 1.333/1.5])
        boundaries[0].thickness = 4
        boundaries[1].water = True
        boundaries[2].thickness = 2
        for time in (0, .25, 1.5, 3.75):
            self.roundtrip(boundaries, time=time)

    def test_previous_wave_time_at_every_boundary(self):
        boundaries = self.boundaries([1/1.5, 1/1.333])
        boundaries[0].thickness, boundaries[1].water = 4, True
        camera, point, target = self.roundtrip(boundaries)
        wrong = inverse_path(point, camera, target, boundaries, time=1.5)
        self.assertIsNotNone(wrong)
        self.assertGreater(np.linalg.norm(wrong-point), .1)

    def test_moving_intermediate_plane_uses_previous_state(self):
        boundaries = self.boundaries([1/1.5, 1.5])
        boundaries[1].normal, boundaries[1].entering = unit([.2, .1, 1]), False
        camera, point, target = self.roundtrip(boundaries)
        wrong_planes = [boundaries[0], replace(boundaries[1], point=boundaries[1].point+[0, 0, -15])]
        wrong = inverse_path(point, camera, target, wrong_planes)
        self.assertIsNotNone(wrong)
        self.assertGreater(np.linalg.norm(wrong-point), .1)

    def test_translated_rotated_nested_planes(self):
        normal, origin = unit([.3, -.2, 1]), np.array([20., -70, 50])
        boundaries = [Boundary(origin-normal*20*i, normal, eta) for i, eta in enumerate([1/1.5, 1.5])]
        self.roundtrip(boundaries, camera=origin+normal*100+np.cross(normal, [20, 10, 7]))

    def test_moving_target_changes_first_surface_projection(self):
        boundaries = self.boundaries([1/1.5, 1.5])
        camera, point, target = self.roundtrip(boundaries)
        moved = inverse_path(point, camera, target+[20, 0, 0], boundaries)
        self.assertIsNotNone(moved)
        self.assertGreater(np.linalg.norm(moved-point), 5)

    def test_total_internal_reflection_at_second_interface(self):
        boundaries = self.boundaries([1, 1.5])
        camera, point = np.array([150., 0, 100]), np.zeros(3)
        self.assertIsNone(replay(point, camera, boundaries))
        self.assertIsNone(inverse_path(point, camera, np.array([-200., 0, -100]), boundaries))

    def test_single_interface_preserves_existing_solution(self):
        camera, target = np.array([25., 7, 100]), np.array([-30., 5, -100])
        boundaries = self.boundaries([1/1.5], thickness=4)
        expected = solve(np.zeros(3), camera, target, boundaries[0].normal, 1/1.5, 4)
        actual = inverse_path(np.zeros(3), camera, target, boundaries)
        np.testing.assert_allclose(actual, expected, atol=.000001)

    def test_intermediate_pane_disocclusion_rejected(self):
        boundaries = self.boundaries([1/1.5, 1.5])
        camera, point, target = self.roundtrip(boundaries)
        boundaries[1].triangle = np.array([[-100., -100, -20], [100, -100, -20], [0, 100, -20]])
        self.assertIsNotNone(inverse_path(point, camera, target, boundaries))
        # Infinite-plane inversion succeeds, but the old ray misses this pane.
        boundaries[1].triangle = np.array([[100., 100, -20], [102, 100, -20], [100, 102, -20]])
        self.assertIsNone(inverse_path(point, camera, target, boundaries))

    def test_finite_triangle_bounds_and_degeneracy(self):
        tri = np.array([[0., 0, 0], [1, 0, 0], [0, 1, 0]])
        self.assertTrue(contains(tri, np.array([.3, .4, 0])))
        self.assertFalse(contains(tri, np.array([.7, .7, 0])))
        self.assertFalse(contains(np.zeros((3, 3)), np.zeros(3)))

    def test_previous_face_orientation_not_current_incident(self):
        old_winding, current_winding = np.array([0., 0, 1]), np.array([1., 0, 0])
        current_incident = unit([-.2, 0, .8])
        current_facing = current_winding.copy()
        expected_old = old_winding*(-1 if current_winding @ current_facing < 0 else 1)
        np.testing.assert_equal(expected_old, [0, 0, 1])
        self.assertGreater(expected_old @ current_incident, 0)
        # Reorienting with the current ray would flip a valid old facing away
        # from an old camera above the plane after a large surface rotation.
        self.assertGreater(expected_old @ np.array([0., 0, 100]), 0)

    def test_missing_tracking_order_and_length_rejected(self):
        boundaries = self.boundaries([1/1.5, 1.5])
        camera, point, target = self.roundtrip(boundaries)
        boundaries[1].tracked = False
        self.assertIsNone(inverse_path(point, camera, target, boundaries))
        self.assertIsNone(inverse_path(point, camera, target, self.boundaries([1]*5)))
        backwards = [boundaries[0], replace(boundaries[1], point=np.array([0., 0, 20]), tracked=True)]
        self.assertIsNone(inverse_path(point, camera, target, backwards))

    def test_signature_order_identity_and_optics_changes(self):
        words = [7, 3, 0x3f400000, 0, 0, 0, 0, 9, 2, 0x3faaaaab, 0, 0, 0, 0, 123]
        baseline = signature(words)
        for index in range(len(words)):
            changed = words.copy(); changed[index] ^= 1
            self.assertNotEqual(signature(changed), baseline)
        self.assertNotEqual(signature(words[::-1]), baseline)
        self.assertNotEqual(signature(words+words), baseline)
        self.assertEqual(signature(words), baseline)

    def test_guide_packing_and_validity_are_integer_metadata(self):
        dtype = np.dtype([('position', '<f4', (4,)), ('identity', '<u4', (4,))])
        record = np.zeros(1, dtype=dtype)
        record['position'][0] = [10, 20, 30, pack_normal([0, 0, -1])]
        record['identity'][0] = [0xfabc0123, *signature([1, 2, 3]), 4]
        self.assertEqual(record.nbytes, 32)
        self.assertEqual(dtype.fields['identity'][1], 16)
        self.assertEqual(record['identity'][0, 3], 4)
        self.assertEqual(record['identity'][0, 0], 0xfabc0123)
        np.testing.assert_allclose(unpack_normal(record['position'][0, 3]), [0, 0, -1], atol=.00015)
        # The packed normal can have NaN bits; validity must never inspect it.
        self.assertTrue(np.isnan(record['position'][0, 3]))

    def test_shader_contract_and_unchanged_allocations(self):
        root = Path(__file__).resolve().parents[1]/'code/renderer_vulkan'
        path = (root/'shaders/pt_transmission_path.glsl').read_text()
        guide = (root/'shaders/pt_transmission_guide.glsl').read_text()
        temporal = (root/'shaders/pt_temporal.comp').read_text()
        native = (root/'vk_pathtrace.c').read_text()
        self.assertIn('PT_TRANSMISSION_INTERFACES=4', guide)
        self.assertIn('struct TransmissionGuide { vec4 position; uvec4 identity; };', guide)
        self.assertIn('min(PT_TRANSMISSION_INTERFACES,int(pc.sampling.y)-1)', path)
        self.assertIn('replayTransmissionPath(point,interfaces,count,true,start,outgoing)', path)
        self.assertIn('previousInterfaceContains(interfaces[i].primitive,point)', path)
        self.assertIn('rp.depthProjection.w', path)
        self.assertIn('tracked=tracked && oldSurface.position.w>0', path)
        self.assertIn('any(notEqual(old.identity,currentGuide.identity))', temporal)
        self.assertIn('currentGuide.identity.w==0u', temporal)
        self.assertIn('bindings[48]', native)
        self.assertIn('VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 41', native)
        for name in ('transmission_guide', 'previous_transmission_guide'):
            self.assertIn(f'buffer_create(&pt.{name}, (VkDeviceSize)width * height * 32', native)


if __name__ == '__main__':
    unittest.main()
