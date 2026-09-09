"""Exact small-domain checks for the shader's map-light RIS normalization."""
import itertools
import math
import unittest


class SamplingTests(unittest.TestCase):
    def test_nonuniform_spatial_proposals_with_global_floor(self):
        values, targets = [1.0, 0.0, 4.0], [0.2, 7.0, 1.5]
        for local in ([.9, .09, .01], [0, 1, 0], [1/3]*3):
            q = [.2/3+.8*p for p in local]
            self.assertAlmostEqual(sum(q), 1)
            self.assertGreaterEqual(min(q), .2/3)
            for m in (1, 2, 3):
                expectation = 0
                for candidates in itertools.product(range(3), repeat=m):
                    weights = [targets[i]/q[i] for i in candidates]
                    total = sum(weights)
                    estimate = sum(w/total*values[i]*total/targets[i]/m for i,w in zip(candidates, weights))
                    expectation += math.prod(q[i] for i in candidates)*estimate
                self.assertAlmostEqual(expectation, sum(values), places=12)

    def test_alias_table_represents_full_support_pdf(self):
        for pdf in ([.85, .1, .05], [1/3]*3, [1.0]):
            n = len(pdf)
            scaled = [p*n for p in pdf]
            small, large = [i for i,p in enumerate(scaled) if p<1], [i for i,p in enumerate(scaled) if p>=1]
            threshold, alias = [1.0]*n, list(range(n))
            while small and large:
                s, l = small.pop(), large.pop()
                threshold[s], alias[s] = scaled[s], l
                scaled[l] += scaled[s]-1
                (small if scaled[l]<1 else large).append(l)
            represented = [0.0]*n
            for i in range(n):
                represented[i] += threshold[i]/n
                represented[alias[i]] += (1-threshold[i])/n
            for a,b in zip(represented,pdf):
                self.assertAlmostEqual(a,b,places=12)

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
