"""Offline optical inversion and split-estimator checks, not GPU validation."""
import math
import unittest
from pathlib import Path
import numpy as np

BIAS = .02


def unit(value):
    value = np.asarray(value, dtype=float)
    return value / np.linalg.norm(value)


def refract(incident, normal, eta):
    cosine = normal @ incident
    k = 1 - eta*eta*(1-cosine*cosine)
    return None if k < 0 else eta*incident-(eta*cosine+math.sqrt(k))*normal


def interface(point, camera, facing, eta, thickness, water, entering, time, reflection=False):
    incident = unit(point-camera)
    if incident @ facing >= -.001:
        return None
    normal = facing.copy()
    if water:
        normal = facing.copy() if entering else -facing
        slope = np.array([.06*math.cos(point[0]*.07+time*1.3), .045*math.cos(point[1]*.09-time*1.1), 0])
        normal = unit(normal-slope+normal*(normal @ slope))
    if normal @ facing < 0:
        normal = -normal
    if normal @ -incident < .001:
        normal = facing
    if reflection:
        outgoing = incident-2*(incident @ normal)*normal
        return None if outgoing @ facing <= .001 else (point+facing*BIAS, outgoing)
    transmitted = refract(incident, normal, eta)
    if transmitted is None or transmitted @ facing >= -.001:
        return None
    if thickness > 0:
        offset = transmitted*(thickness/max(abs(transmitted @ facing), .001))
        offset -= incident*((offset @ facing)/min(incident @ facing, -.001))
        return point+offset-facing*BIAS, incident
    return point-facing*BIAS, unit(transmitted)


def solve(seed, camera, target, facing, eta, thickness=0, water=False, entering=True, time=0, height=720, projection=1, reflection=False):
    point = seed.copy()
    side = (target-seed) @ facing
    if (camera-seed) @ facing <= .001 or (side <= BIAS if reflection else side >= -BIAS):
        return None
    u = unit(np.cross([0, 0, 1] if abs(facing[2]) < .999 else [0, 1, 0], facing))
    v = np.cross(facing, u)
    path_distance = np.linalg.norm(seed-camera)+np.linalg.norm(target-seed) if reflection else np.linalg.norm(target-camera)
    footprint = max(.02, path_distance*2/(height*abs(projection)))
    epsilon, tolerance = max(.01, footprint*.05), max(.005, footprint*.1)
    def residual(p):
        ray = interface(p, camera, facing, eta, thickness, water, entering, time, reflection)
        if ray is None:
            return None
        start, outgoing = ray
        distance = ((target-start) @ facing)/(outgoing @ facing)
        if distance <= 0:
            return None
        error = start+outgoing*distance-target
        return np.array([error @ u, error @ v])
    for _ in range(8):
        r = residual(point)
        if r is None or not np.isfinite(r).all():
            return None
        if np.linalg.norm(r) <= tolerance:
            return point
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
    return point if r is not None and np.linalg.norm(r) <= tolerance else None


class TransmissionMath(unittest.TestCase):
    def roundtrip(self, camera, point, normal, eta, thickness=0, water=False, entering=True, time=0, distance=100):
        camera, point, normal = map(lambda x: np.array(x, dtype=float), (camera, point, normal))
        ray = interface(point, camera, normal, eta, thickness, water, entering, time)
        self.assertIsNotNone(ray)
        start, outgoing = ray
        target = start+outgoing*distance
        seed = point + np.cross(normal, [.3, .4, .5])*4
        found = solve(seed, camera, target, normal, eta, thickness, water, entering, time)
        self.assertIsNotNone(found)
        np.testing.assert_allclose(found, point, atol=.08)

    def test_thin_pane_roundtrip(self):
        for theta in (0, 20, 50, 70):
            for thickness in (1, 4, 16):
                self.roundtrip([100*math.tan(math.radians(theta)), 4, 100], [0, 0, 0], [0, 0, 1], 1/1.5, thickness)

    def test_ior_one_matches_straight_ray(self):
        camera, target = np.array([15., 7, 100]), np.array([-30., 5, -60])
        expected = camera+(target-camera)*(100/160)
        for thickness in (0, 4, 16):
            found = solve(np.zeros(3), camera, target, np.array([0., 0, 1]), 1, thickness)
            # The ray bias is a normal offset, so allow its subpixel displacement.
            np.testing.assert_allclose(found, expected, atol=.02)

    def test_single_water_interface(self):
        self.roundtrip([40, 20, 100], [0, 0, 0], [0, 0, 1], 1/1.333)

    def test_time_varying_wave_roundtrip(self):
        for time in (0, .25, 1, 3.75):
            self.roundtrip([45, -15, 100], [11, -20, 0], [0, 0, 1], 1/1.333, water=True, time=time)

    def test_underwater_wave_orientation(self):
        for time in (0, .25, 1):
            self.roundtrip([5, 7, -100], [0, 0, 0], [0, 0, -1], 1.333, water=True, entering=False, time=time)

    def test_rotated_translated_interface(self):
        normal = unit([.3, -.2, 1])
        point = np.array([20., -70, 50])
        self.roundtrip(point+normal*100+np.cross(normal, [20, 10, 7]), point, normal, 1/1.5, 5)

    def test_total_internal_reflection_rejected(self):
        self.assertIsNone(interface(np.zeros(3), np.array([150., 0, -100]), np.array([0., 0, -1]), 1.5, 0, False, False, 0))

    def test_target_on_wrong_side_rejected(self):
        self.assertIsNone(solve(np.zeros(3), np.array([0., 0, 100]), np.array([0., 0, 20]), np.array([0., 0, 1]), 1/1.5))

    def test_moving_target_changes_reprojection(self):
        camera, normal = np.array([0., 0, 100]), np.array([0., 0, 1])
        old = solve(np.zeros(3), camera, np.array([-30., 0, -100]), normal, 1/1.5, 4)
        new = solve(np.zeros(3), camera, np.array([30., 0, -100]), normal, 1/1.5, 4)
        self.assertIsNotNone(old); self.assertIsNotNone(new)
        self.assertGreater(np.linalg.norm(new-old), 20)

    def test_previous_wave_time_matters(self):
        camera, point, normal = np.array([45., -15, 100]), np.array([11., -20, 0]), np.array([0., 0, 1])
        start, outgoing = interface(point, camera, normal, 1/1.333, 0, True, True, 0)
        target = start+outgoing*80
        old = solve(point, camera, target, normal, 1/1.333, water=True, time=0)
        wrong_time = solve(point, camera, target, normal, 1/1.333, water=True, time=1.5)
        np.testing.assert_allclose(old, point, atol=.03)
        self.assertIsNotNone(wrong_time)
        self.assertGreater(np.linalg.norm(wrong_time-old), .1)

    def test_split_transport_keeps_first_lobe_and_energy(self):
        # Opaque-first, reflective-first and transmissive-first paths all retain
        # their first-lobe labels through later mixed scattering and attenuation.
        for first in ("opaque", "reflect", "transmit"):
            d = s = t = 0.; e = original = 1.
            if first == "reflect": s, e = e, 0.
            if first == "transmit": t, e = e, 0.
            for bd, bs, factor, absorption in ((.3, .4, 1.2, .8), (.1, .6, .9, .5)):
                brdf = bd+bs
                self.assertAlmostEqual((d*brdf+e*bd)+(s*brdf+e*bs)+t*brdf, original*brdf)
                d, s, t, e = (d*brdf+e*bd)*factor, (s*brdf+e*bs)*factor, t*brdf*factor, 0.
                d *= absorption; s *= absorption; t *= absorption
                original *= brdf*factor*absorption
                self.assertAlmostEqual(d+s+t+e, original)
            if first == "transmit":
                self.assertEqual(d+s, 0)
                self.assertGreater(t, 0)

    def test_shader_binding_contract(self):
        root = Path(__file__).resolve().parents[1]/"code/renderer_vulkan"
        native = (root/"vk_pathtrace.c").read_text()
        self.assertIn("bindings[48]", native)
        self.assertIn("VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 41", native)
        for binding in range(42, 48):
            self.assertIn(f"bind_buffer({binding},", native)
        for binding in (42, 45, 47):
            self.assertIn(f"binding={binding},", (root/"shaders/pt_integrator.glsl").read_text())
        for binding in range(42, 48):
            self.assertIn(f"binding={binding},", (root/"shaders/pt_temporal.comp").read_text())
        self.assertIn("reconstruction->depth_projection[3] = pt.previous_material_time", native)


if __name__ == "__main__":
    unittest.main()
