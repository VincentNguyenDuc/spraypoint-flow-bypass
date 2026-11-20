import importlib.util
import math
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "experiments/analysis"))
from workload_cdf import WorkloadCdf, sampled_size, segment_mean, size_integral


class CdfTests(unittest.TestCase):
    def test_uniform_quantile_rounding_and_cutoff(self):
        cdf = WorkloadCdf(((0, 0), (2, 1)))
        # X~U[0,2] rounds/clamps to 1 with probability .75, else to 2.
        self.assertEqual(cdf.moments(), (1, 1.25))
        self.assertEqual(cdf.moments(1), (.75, .75))
        self.assertEqual(cdf.moments(0), (0, 0))
        self.assertEqual(cdf.mean_for_arrival_rate(), 1)
        self.assertAlmostEqual(cdf.coverage(1)["byte_fraction"], .6)

    def test_initial_mass_and_zero_width_segments(self):
        cdf = WorkloadCdf(((1.5, .2), (1.5, .6), (4, 1)))
        self.assertEqual(cdf.moments(1), (0, 0))
        probability, _ = cdf.moments(2)
        self.assertAlmostEqual(probability, .76)

    def test_plateau_cdf_has_no_mass(self):
        cdf = WorkloadCdf(((1, .5), (100, .5), (100, 1)))
        self.assertEqual(cdf.moments(), (1, 50.5))
        self.assertEqual(cdf.moments(1), (.5, .5))

    def test_all_zero_sizes_clamp_to_one(self):
        cdf = WorkloadCdf(((0, .3), (0, 1)))
        self.assertEqual(cdf.moments(), (1, 1))
        self.assertEqual(cdf.coverage(1)["byte_fraction"], 1)

    def test_integral_against_exhaustive_bins(self):
        for start, end in [(0, 8), (.3, 7.2), (1.5, 20.25)]:
            # Midpoint quadrature is exact here, with all half-byte boundaries split.
            edges = sorted({start, end, *[x + .5 for x in range(21) if start < x + .5 < end]})
            expected = sum((b-a) * sampled_size((a+b)/2) for a,b in zip(edges, edges[1:]))
            self.assertAlmostEqual(size_integral(end)-size_integral(start), expected)
            self.assertAlmostEqual(segment_mean(start, end), expected / (end-start))

    def test_narrow_large_segments_keep_exact_constant_rounded_size(self):
        for start in [1_000_000.1, 1_000_000_000.1, 4_000_000_000.1]:
            with self.subTest(start=start):
                end = math.nextafter(start, math.inf)
                expected = sampled_size(start)
                cdf = WorkloadCdf(((start, 0), (end, 1)))
                self.assertEqual(cdf.moments(), (1, expected))
                self.assertEqual(cdf.moments(expected), (1, expected))
                self.assertEqual(cdf.moments(expected - 1), (0, 0))

    def test_subnormal_segment_width_does_not_overflow_density(self):
        for end in [1e-309, 1e-320, math.nextafter(0.0, math.inf)]:
            with self.subTest(end=end):
                cdf = WorkloadCdf(((0, 0), (end, 1)))
                self.assertEqual(cdf.moments(), (1, 1))
                self.assertEqual(cdf.moments(1), (1, 1))
                row = cdf.coverage(1)
                self.assertEqual(row["flow_fraction"], 1)
                self.assertEqual(row["byte_fraction"], 1)
                self.assertEqual(row["mean_sampled_bytes"], 1)

    def test_narrow_segments_crossing_half_byte_boundary(self):
        for boundary in [1.5, 1_000_000.5, 4_000_000_000.5]:
            with self.subTest(boundary=boundary):
                start = math.nextafter(boundary, -math.inf)
                end = math.nextafter(boundary, math.inf)
                low = sampled_size(start)
                cdf = WorkloadCdf(((start, 0), (end, 1)))
                self.assertEqual(cdf.moments(), (1, low + .5))
                self.assertEqual(cdf.moments(low), (.5, .5 * low))
                self.assertEqual(segment_mean(start, boundary), low)
                self.assertEqual(segment_mean(boundary, end), low + 1)

    def test_large_segment_spanning_multiple_bins(self):
        start, end = 4_000_000_000.25, 4_000_000_002.75
        # Widths .25, 1, 1, .25 at sizes N, N+1, N+2, N+3.
        self.assertEqual(segment_mean(start, end), 4_000_000_001.5)
        cdf = WorkloadCdf(((start, 0), (end, 1)))
        self.assertEqual(cdf.moments(), (1, 4_000_000_001.5))

    def test_invalid_inputs(self):
        for points in [(), ((1, .9),), ((1, .8), (0, 1)), ((0, 0), (math.inf, 1)),
                       ((1, -.1), (2, 1)), ((1, 1.1),), ((2**32, 1),)]:
            with self.subTest(points=points), self.assertRaises(ValueError):
                WorkloadCdf(points)
        with self.assertRaises(ValueError):
            WorkloadCdf(((1, 1),)).coverage(-1)

    def test_real_workload_coverage_is_monotonic(self):
        for name in ["websearch", "hadoop"]:
            cdf = WorkloadCdf.read(ROOT / f"experiments/workloads/{name}.cdf")
            rows = [cdf.coverage(n) for n in [0, 10000, 100000, 1000000, 30000000]]
            for key in ["flow_fraction", "byte_fraction"]:
                values = [row[key] for row in rows]
                self.assertEqual(values, sorted(values))
                self.assertAlmostEqual(values[-1], 1)


if __name__ == "__main__":
    unittest.main()
