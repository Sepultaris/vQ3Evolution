"""Exact small-domain checks for the shader's map-light RIS normalization."""
import itertools
import unittest


class SamplingTests(unittest.TestCase):
    def test_uniform_candidates_with_duplicates_are_unbiased(self):
        # Integrand includes visibility; target deliberately does not. An
        # occluded candidate still competes for reservoir selection in GLSL.
        values = [1.0, 0.0, 4.0]
        targets = [0.2, 7.0, 1.5]
        n = len(values)
        for m in (1, 2, 3):
            expectation = 0.0
            for candidates in itertools.product(range(n), repeat=m):
                total = sum(targets[i] for i in candidates)
                # Integrate the weighted reservoir's selection analytically.
                estimate = sum((targets[i] / total) * values[i] * total /
                               targets[i] * n / m for i in candidates)
                expectation += estimate / n ** m
            self.assertAlmostEqual(expectation, sum(values), places=12)

    def test_area_power_solid_angle_pdf(self):
        areas, powers = [2.0, 8.0], [3.0, 1.0]
        total = sum(a * p for a, p in zip(areas, powers))
        for area, power in zip(areas, powers):
            distance_squared, cosine = 400.0, 0.5
            selected_probability = area * power / total
            expected = selected_probability / area * distance_squared / cosine
            shader_pdf = power * distance_squared / (total * cosine)
            self.assertAlmostEqual(expected, shader_pdf)


if __name__ == "__main__":
    unittest.main()
