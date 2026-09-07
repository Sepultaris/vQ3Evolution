"""Physical invariants plus checks of real native material-fixture screenshots."""
import argparse
import math
import unittest
from pathlib import Path


def fresnel(cosine, eta_i, eta_t):
    sin_t2 = (eta_i / eta_t) ** 2 * (1 - cosine * cosine)
    if sin_t2 >= 1:
        return 1.0
    ct = math.sqrt(1 - sin_t2)
    rs = (eta_i * cosine - eta_t * ct) / (eta_i * cosine + eta_t * ct)
    rp = (eta_t * cosine - eta_i * ct) / (eta_t * cosine + eta_i * ct)
    return (rs * rs + rp * rp) / 2


class Transport(unittest.TestCase):
    def test_normal_incidence(self):
        self.assertAlmostEqual(fresnel(1, 1, 1.5), 0.04)
        self.assertAlmostEqual(fresnel(1, 1, 1.333), 0.02037318784)

    def test_total_internal_reflection(self):
        self.assertEqual(fresnel(math.cos(math.radians(50)), 1.5, 1), 1)
        self.assertLess(fresnel(math.cos(math.radians(30)), 1.5, 1), 1)

    def test_snell_and_pane_identity(self):
        for angle in (0, 10, 30, 60, 80):
            theta = math.radians(angle)
            for eta in (1, 1.333, 1.5, 2.4):
                transmitted = math.asin(math.sin(theta) / eta)
                self.assertAlmostEqual(eta * math.sin(transmitted), math.sin(theta))
                offset = 18 * (math.tan(transmitted) - math.tan(theta))
                if eta == 1:
                    self.assertAlmostEqual(offset, 0)
                elif angle:
                    self.assertLess(offset, 0)

    def test_pane_energy_and_absorption(self):
        for angle in (0, 30, 60, 80):
            f = fresnel(math.cos(math.radians(angle)), 1, 1.5)
            for absorption in (0, .01, 1):
                a = math.exp(-absorption * 18)
                t = (1 - f) ** 2 * a / (1 - f * f * a * a)
                r = f + (1 - f) ** 2 * f * a * a / (1 - f * f * a * a)
                self.assertLessEqual(t + r, 1 + 1e-12)
                if absorption == 0:
                    self.assertAlmostEqual(t + r, 1)
                else:
                    self.assertLess(t + r, 1)

    def test_volume_eta_cancellation(self):
        self.assertAlmostEqual((1 / 1.5) ** 2 * 1.5 ** 2, 1)
        self.assertAlmostEqual(math.exp(-.01 * 20) * math.exp(-.01 * 30), math.exp(-.01 * 50))

    def test_lobe_partition(self):
        # Exact estimator partition, including a delta transmission and emission.
        d = s = 0.0
        unscattered = original = 1.0
        for diffuse, specular, factor in ((.3, .2, 1.2), (.1, .8, .7), (.4, .1, 1.8)):
            brdf = diffuse + specular
            self.assertAlmostEqual(d * brdf + unscattered * diffuse +
                                   s * brdf + unscattered * specular, original * brdf)
            d = (d * brdf + unscattered * diffuse) * factor
            s = (s * brdf + unscattered * specular) * factor
            unscattered = 0
            original *= brdf * factor
            self.assertAlmostEqual(d + s, original)


def check_histories(folder):
    import numpy as np
    from PIL import Image
    d, s = [np.asarray(Image.open(folder / f"pt_split_{name}_history.jpg").convert("RGB"), dtype=float)/255
            for name in ("diffuse", "reflection")]
    world = (d[:, :, 1] > .2) & (d[:, :, 0] < .1) & (d[:, :, 2] < .1)
    assert world.mean() > .1, "No reusable diffuse world history"
    difference = float((d[:, :, 1]-s[:, :, 1])[world].mean())
    changed = float((np.abs(d-s).max(axis=2)[world] > .1).mean())
    print(f"Independent history: diffuse green minus reflection green {difference:.3f}, {changed:.1%} differing world pixels")
    # Rough walls can legitimately use the same cap in both channels. Test the
    # surfaces where both histories survive, not a global average diluted by
    # those walls. Glossy regions must use shorter independent reflection history.
    common = world & (s[:, :, 1] > .2) & (s[:, :, 0] < .1) & (s[:, :, 2] < .1)
    delta = d[:, :, 1]-s[:, :, 1]
    shorter = common & (delta > .15)
    assert common.mean() > .1
    assert shorter.sum()/common.sum() > .1 and delta[shorter].mean() > .2, "No independently shortened reflection history"
    assert (delta[common] < -.1).mean() < .05, "Reflection history unexpectedly outlives diffuse history"


def check_images(folder):
    import numpy as np
    from PIL import Image

    def read(name):
        return np.asarray(Image.open(folder / f"pt_pbr_{name}.jpg").convert("RGB"), dtype=float) / 255

    beauty, diffuse, specular, emission, normals, orm, dielectric = [
        read(n) for n in ("beauty", "diffuse", "specular", "emission", "normals", "orm", "dielectric")]
    assert all(a.shape == beauty.shape for a in (diffuse, specular, emission, normals, orm, dielectric))
    # Erode JPEG masks; never measure antialiased silhouettes as material interiors.
    def erode(mask):
        result = mask.copy()
        result[[0, -1], :] = False
        result[:, [0, -1]] = False
        for y, x in ((0, 1), (0, -1), (1, 0), (-1, 0)):
            result &= np.roll(mask, (y, x), axis=(0, 1))
        return result

    control = erode((abs(dielectric[:, :, 0] - 1/3) < .025) & (dielectric[:, :, 1] > .94))
    pane = erode((abs(dielectric[:, :, 0] - .5) < .025) & (dielectric[:, :, 1] > .94))
    glass = erode((abs(dielectric[:, :, 0] - .5) < .025) & (dielectric[:, :, 1] < .03))
    water = erode((abs(dielectric[:, :, 0] - 1.333/3) < .025) & (dielectric[:, :, 2] > .94))
    for name, mask in (("control", control), ("pane", pane), ("glass", glass), ("water", water)):
        assert mask.mean() > .005, f"Missing {name} geometry"
        assert specular[mask].mean() > .1, f"No optical transport in {name}"
        assert diffuse[mask].mean() < .015, f"Dielectric light leaked into diffuse: {name}"
    # The rear checker is separable in screen X/Y. Recover its unobstructed
    # appearance from full background strips, independently of the ray shader.
    background = (np.abs(normals - np.array([0, .5, .5])).max(axis=2) < .035) & (emission.max(axis=2) > .5)
    row = int(background.sum(axis=1).argmax())
    col = int(background.sum(axis=0).argmax())
    hue = beauty[:, :, 0] > beauty[:, :, 2]
    predicted = hue[row, :][None, :] ^ hue[:, col][:, None] ^ hue[row, col]
    known = erode(background)
    assert (predicted[known] == hue[known]).mean() > .94, "Checker reconstruction failed"
    errors = {}
    for name, mask in (("control", control), ("pane", pane), ("glass", glass), ("water", water)):
        # Only the projected rectangle of the background has a checker reference.
        mask &= background[row, :][None, :] & background[:, col][:, None]
        assert mask.sum() > 100
        errors[name] = float((predicted[mask] != hue[mask]).mean())
    print("Checker displacement fractions:", errors)
    assert errors["control"] < .06, "IOR=1 must not distort the background"
    assert errors["pane"] > errors["control"] + .015, "Pane refraction is missing"
    assert errors["glass"] > .1 and errors["water"] > .1, "Volume ray bending is missing"
    # An ORM map must produce both metallic and dielectric regions, with a
    # continuous roughness gradient. The native normal map must vary the normals.
    pbr = (orm[:, :, 0] > .08) & (orm[:, :, 0] < .8) & (dielectric.max(axis=2) < .025)
    assert pbr.mean() > .02
    assert orm[:, :, 1][pbr].std() > .2, "No texture-driven metallic variation"
    assert orm[:, :, 0][pbr].std() > .1, "No texture-driven roughness variation"
    assert normals[:, :, 1][pbr].std() > .08, "Normal mapping is missing"
    # Recompose in linear radiance (inverse of the renderer's tone curve).
    grid = np.linspace(0, 20, 200001)
    mapped = np.clip((grid*(2.51*grid+.03))/(grid*(2.43*grid+.59)+.14), 0, 1)**(1/2.2)
    inverse = lambda a: np.interp(a, mapped, grid)
    summed = inverse(diffuse) + inverse(specular) + inverse(emission)
    recomposed = np.clip((summed*(2.51*summed+.03))/(summed*(2.43*summed+.59)+.14), 0, 1)**(1/2.2)
    error = float(np.abs(recomposed-beauty).mean())
    print(f"Tone-recomposed component MAE: {error*255:.3f}/255")
    assert error < .02, "Diffuse + reflection + emission does not reproduce beauty"
    print("PASS: independent lighting channels, normal/ORM maps, identity pane and refractive glass/water")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshots", nargs="?", type=Path)
    parser.add_argument("--histories", action="store_true")
    args = parser.parse_args()
    result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(Transport))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.screenshots:
        check_images(args.screenshots)
        if args.histories:
            check_histories(args.screenshots)
