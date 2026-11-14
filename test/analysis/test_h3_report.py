"""Synthetic H3 aggregates only; never open experimental results or caches."""

import contextlib
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "experiments/analysis"))
import h3_report as h3


def digest(text):
    return hashlib.sha256(text.encode()).hexdigest()


def triple(seed=1, scenario="fixture", load=.5, tails=(10, 8, 6), goodputs=(5, 4, 6)):
    rows = []
    for mode, tail, goodput in zip(h3.MODES, tails, goodputs):
        row = dict(scenario=scenario, load=load, seed=seed, bypass=mode,
                   flow_fingerprint=digest(f"flows:{scenario}:{load}:{seed}"),
                   topology_fingerprint=digest(f"topology:{seed}"),
                   flows=10000, analyzed_flows=8000, unfinished=0,
                   all_flows=8000, small_flows=6400, small_adjacent_flows=1600,
                   small_other_flows=4800, large_flows=1600,
                   small_p50_us=tail / 2, small_p99_us=tail,
                   small_adjacent_p50_us=tail / 4, small_adjacent_p99_us=tail / 2,
                   small_other_p50_us=tail, small_other_p99_us=tail * 2,
                   large_mean_goodput_gbps=goodput, large_p01_goodput_gbps=goodput / 3,
                   small_byte_share=.6, adjacent_small_byte_share=.15,
                   mean_utilization=.2, max_utilization=.4, hotspot_ratio=2,
                   mean_busiest_neighbor_share=.3, fabric_drops=0)
        for name in h3.CLASSES:
            row[name + "_beyond_p99"] = row[name + "_flows"] // 100
        rows.append(row)
    return rows


class H3Tests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="cse561-h3-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.batch = self.root / "batch"
        (self.batch / "analysis").mkdir(parents=True)
        self.source = self.batch / "analysis/h2-runs.csv"
        self.output = self.root / "report"

    def write(self, rows):
        fields = list(rows[0]) if rows else sorted(h3.REQUIRED_COLUMNS)
        with self.source.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fields)
            writer.writeheader()
            writer.writerows(rows)

    def analyze(self, rows, **options):
        self.write(rows)
        runs, _ = h3.load_csv(self.source)
        return h3.analyze(runs, **options)

    def test_all_small_is_input_quantile_and_increment_is_separate(self):
        result = self.analyze(triple())
        self.assertEqual(result["runs"][0]["small_p99_us"], 10)
        self.assertNotEqual(result["runs"][0]["small_p99_us"], .25 * 5 + .75 * 20)
        increment = next(row for row in result["comparisons"]
                         if row["comparison"] == "shortest_vs_adjacent")
        self.assertEqual(increment["small_p99_us_ratio"], .75)
        self.assertEqual(increment["small_p99_gain_pct"], 25)
        self.assertEqual(increment["incremental_small_flow_eligibility_share"], .75)
        self.assertAlmostEqual(increment["incremental_total_byte_eligibility_share"], .45)
        self.assertEqual(increment["large_mean_goodput_gbps_change_pct"], 50)
        self.assertEqual(increment["mean_busiest_neighbor_share_ratio"], 1)

    def test_mean_of_paired_ratios_is_not_ratio_of_means(self):
        result = self.analyze(triple(seed=1, tails=(1, 2, 1)) +
                              triple(seed=2, tails=(10, 10, 8)))
        summary = next(row for row in result["summary"]
                       if row["comparison"] == "adjacent_vs_none" and row["metric"] == "small_p99_us")
        self.assertEqual(summary["mean_of_paired_ratios"], 1.5)
        self.assertNotAlmostEqual(summary["mean_of_paired_ratios"], 12 / 11)
        self.assertEqual((summary["paired_seeds"], summary["defined_ratios"]), (2, 2))

    def test_pareto_only_compares_modes_within_same_scenario_load_seed(self):
        rows = (triple() + triple(seed=2, tails=(100, 80, 60), goodputs=(50, 40, 60)) +
                triple(scenario="different_cutoff", tails=(1, .8, .6), goodputs=(500, 400, 600)) +
                triple(load=.7, tails=(200, 160, 120), goodputs=(.5, .4, .6)))
        result = self.analyze(rows)
        self.assertEqual(len(result["pareto"]), 12)
        for row in result["pareto"]:
            self.assertEqual(row["on_frontier"], row["bypass"] == "shortest")
            self.assertEqual(row["dominated_by"], "" if row["bypass"] == "shortest" else "shortest")

    def test_pareto_ties_and_tradeoffs_remain_nondominated(self):
        result = self.analyze(triple(tails=(10, 8, 8), goodputs=(5, 4, 4)))
        self.assertTrue(all(row["on_frontier"] for row in result["pareto"]))

    def test_duplicates_fail_even_with_allow_incomplete(self):
        with self.assertRaisesRegex(ValueError, "Duplicate run key"):
            self.analyze(triple() + triple()[:1], allow_incomplete=True)

    def test_hash_and_population_mismatches_always_fail(self):
        for field in h3.FINGERPRINTS:
            rows = triple()[:2]
            rows[1][field] = digest("different")
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "mismatch"):
                self.analyze(rows, allow_incomplete=True)
        rows = triple()
        rows[1]["flows"] += 1
        with self.assertRaisesRegex(ValueError, "population count mismatch"):
            self.analyze(rows)
        rows = triple()
        rows[1]["small_byte_share"] = .61
        with self.assertRaisesRegex(ValueError, "byte-share mismatch"):
            self.analyze(rows)

    def test_missing_modes_and_wholly_missing_requested_seed(self):
        rows = triple(seed=1) + triple(seed=2)[:2]
        with self.assertRaisesRegex(ValueError, "Incomplete three-mode pairing"):
            self.analyze(rows)
        result = self.analyze(rows, allow_incomplete=True, expected_seeds=[1, 2, 3])
        self.assertEqual(result["included_triples"], 1)
        self.assertEqual(len(result["comparisons"]), 3)
        self.assertEqual(len(result["exclusions"]), 2)
        self.assertEqual(result["exclusions"][0]["missing_modes"], "shortest")
        self.assertEqual(result["exclusions"][1]["missing_modes"], "none;adjacent;shortest")
        self.assertEqual(sum(row["included"] for row in result["runs"]), 3)

    def test_all_incomplete_can_be_reported_without_implied_comparison(self):
        self.write(triple()[:1])
        result = h3.report(self.source, self.output, allow_incomplete=True)
        self.assertEqual(result["included_triples"], 0)
        self.assertFalse(result["summary"])
        text = (self.output / "H3.md").read_text()
        self.assertIn("Complete three-mode groups: 0", text)
        self.assertIn("adjacent;shortest", text)
        with (self.output / "h3-summary.csv").open(newline="") as stream:
            self.assertIn("mean_of_paired_ratios", csv.DictReader(stream).fieldnames)

    def test_row_order_does_not_change_comparisons_or_exclusions(self):
        rows = triple(seed=1) + triple(seed=2)[:2]
        forward = self.analyze(rows, allow_incomplete=True)
        reverse = self.analyze(list(reversed(rows)), allow_incomplete=True)
        for field in ("runs", "pareto", "exclusions", "warnings"):
            self.assertEqual(forward[field], reverse[field])
        for field in ("comparisons", "summary"):
            # NaN is unequal to itself; JSON preserves its stable serialized sentinel.
            self.assertEqual(json.dumps(forward[field], sort_keys=True),
                             json.dumps(reverse[field], sort_keys=True))

    def test_infinite_fct_withholds_ratios_summary_and_whole_frontier(self):
        rows = triple(seed=1) + triple(seed=2)
        rows[0]["small_p99_us"] = math.inf
        rows[0]["unfinished"] = 101
        result = self.analyze(rows)
        first = result["comparisons"][0]
        self.assertTrue(math.isnan(first["small_p99_us_ratio"]))
        self.assertEqual(first["small_p99_us_ratio_status"], "undefined_nonfinite")
        summary = next(row for row in result["summary"]
                       if row["comparison"] == "adjacent_vs_none" and row["metric"] == "small_p99_us")
        self.assertTrue(math.isnan(summary["mean_of_paired_ratios"]))
        self.assertEqual((summary["defined_ratios"], summary["paired_seeds"]), (1, 2))
        self.assertTrue(all(row["on_frontier"] == "" for row in result["pareto"] if row["seed"] == 1))
        self.assertIn("unfinished_flows", {row["code"] for row in result["warnings"]})

    def test_zero_denominator_nan_metric_and_overflow_are_explicit(self):
        rows = triple()
        rows[0]["large_p01_goodput_gbps"] = 0
        rows[1]["hotspot_ratio"] = math.nan
        result = self.analyze(rows)
        row = result["comparisons"][0]
        self.assertEqual(row["large_p01_goodput_gbps_ratio_status"], "undefined_zero_denominator")
        self.assertEqual(row["hotspot_ratio_ratio_status"], "undefined_nonfinite")
        self.assertTrue(math.isnan(h3.ratio(1e308, 1e-308)[0]))
        self.assertTrue(math.isnan(h3.ratio(1, math.inf)[0]))
        self.assertTrue(math.isnan(h3.ratio(math.inf, math.inf)[0]))
        self.assertTrue(math.isnan(h3.change_pct(1e308)))

    def test_empty_classes_are_explicit_and_frontier_withheld(self):
        rows = triple()
        for row in rows:
            for name in h3.CLASSES:
                row[name + "_flows"] = 0
                row[name + "_beyond_p99"] = 0
            row["analyzed_flows"] = 0
            for field in h3.REQUIRED_METRICS:
                row[field] = math.nan
        result = self.analyze(rows)
        self.assertTrue(all(row["on_frontier"] == "" for row in result["pareto"]))
        self.assertTrue(math.isnan(result["comparisons"][-1]["incremental_small_flow_eligibility_share"]))

    def test_invalid_populations_probabilities_keys_and_metrics(self):
        edits = [
            {"small_flows": 6399}, {"analyzed_flows": 7999}, {"unfinished": 10001},
            {"flows": -1}, {"flows": "1.5"}, {"small_beyond_p99": 65},
            {"small_byte_share": math.nan}, {"adjacent_small_byte_share": .7},
            {"flow_fingerprint": "not-a-digest"}, {"load": math.inf}, {"load": 0},
            {"seed": -1}, {"bypass": "unknown"}, {"scenario": ""},
            {"small_p99_us": -math.inf}, {"small_p50_us": 100},
        ]
        for changes in edits:
            rows = triple()
            rows[0].update(changes)
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.analyze(rows)

    def test_missing_metrics_fail_and_optional_network_fields_are_optional(self):
        rows = triple()
        for row in rows:
            del row["small_p99_us"]
        with self.assertRaisesRegex(ValueError, "Missing required columns"):
            self.analyze(rows)
        rows = triple()
        for row in rows:
            for field in h3.OPTIONAL_METRICS:
                row.pop(field, None)
        self.write(rows)
        h3.report(self.source, self.output)
        self.assertIn("unavailable", (self.output / "H3.md").read_text())

    def test_output_provenance_and_no_source_writes(self):
        self.write(triple())
        before = self.source.read_bytes()
        before_stamp = h3.stamp(self.source)
        result = h3.report(self.source, self.output)
        self.assertEqual(self.source.read_bytes(), before)
        self.assertEqual(h3.stamp(self.source), before_stamp)
        self.assertEqual(list(self.source.parent.iterdir()), [self.source])
        metadata = json.loads((self.output / "h3-provenance.json").read_text())
        self.assertEqual(metadata["input"]["sha256"], hashlib.sha256(before).hexdigest())
        self.assertEqual(metadata["included_triples"], 1)
        self.assertEqual(metadata["warning_count"], len(result["warnings"]))
        self.assertRegex(metadata["script_sha256"], r"^[0-9a-f]{64}$")
        self.assertEqual(len(list(self.output.iterdir())), 8)
        text = (self.output / "H3.md").read_text()
        for phrase in ("not a ratio of means", "not rank thresholds", "no averaging",
                       "FCT-based timeout inference", "small_tail_sample", "not host sender counts"):
            self.assertIn(phrase, text)

    def test_output_must_be_new_outside_batch_even_through_symlink(self):
        self.write(triple())
        for output in (self.batch / "new", self.source.parent / "new"):
            with self.subTest(output=output), self.assertRaisesRegex(ValueError, "outside"):
                h3.report(self.source, output)
            self.assertFalse(output.exists())
        alias = self.root / "batch-link"
        alias.symlink_to(self.batch, target_is_directory=True)
        with self.assertRaisesRegex(ValueError, "outside"):
            h3.report(self.source, alias / "new")
        with self.assertRaisesRegex(ValueError, "outside"):
            h3.report(self.source, self.batch / "new", input_batch=self.source.parent)
        self.output.mkdir()
        sentinel = self.output / "keep"
        sentinel.write_text("untouched")
        with self.assertRaisesRegex(ValueError, "must be new"):
            h3.report(self.source, self.output)
        self.assertEqual(sentinel.read_text(), "untouched")
        with self.assertRaisesRegex(ValueError, "inside --input-batch"):
            h3.report(self.source, self.root / "elsewhere", input_batch=self.root / "wrong")

    def test_unstable_input_is_rejected_before_output(self):
        self.write(triple())
        real_stamp = h3.stamp(self.source)
        changed = (*real_stamp[:-1], real_stamp[-1] + 1)
        with patch.object(h3, "stamp", side_effect=[real_stamp, changed]):
            with self.assertRaisesRegex(ValueError, "Input changed while reading"):
                h3.report(self.source, self.output)
        self.assertFalse(self.output.exists())
        with patch.object(h3, "stamp", side_effect=[real_stamp, real_stamp, changed]):
            with self.assertRaisesRegex(ValueError, "Input changed during analysis"):
                h3.report(self.source, self.output)
        self.assertFalse(self.output.exists())

    def test_invalid_expected_seeds_and_empty_csv(self):
        self.write(triple())
        for seeds in ([1, 1], [-1]):
            with self.assertRaisesRegex(ValueError, "distinct nonnegative"):
                h3.report(self.source, self.output, expected_seeds=seeds)
        self.write([])
        with self.assertRaisesRegex(ValueError, "no runs"):
            h3.report(self.source, self.output)

    def test_malformed_csv_is_rejected(self):
        self.write(triple())
        content = self.source.read_text()
        self.source.write_text("scenario," + content)
        with self.assertRaisesRegex(ValueError, "Duplicate CSV column"):
            h3.load_csv(self.source)
        self.source.write_text(content + "extra,fields\n")
        with self.assertRaisesRegex(ValueError, "wrong number of fields"):
            h3.load_csv(self.source)

    def test_cli_success_and_incomplete_failure(self):
        self.write(triple())
        with contextlib.redirect_stdout(io.StringIO()) as stdout:
            h3.main([str(self.source), "--output-dir", str(self.output), "--expected-seeds", "1"])
        self.assertIn("1 matched groups", stdout.getvalue())
        self.write(triple()[:2])
        with contextlib.redirect_stderr(io.StringIO()) as stderr:
            with self.assertRaises(SystemExit) as error:
                h3.main([str(self.source), "--output-dir", str(self.root / "missing")])
        self.assertEqual(error.exception.code, 2)
        self.assertIn("Incomplete three-mode pairing", stderr.getvalue())
        self.assertFalse((self.root / "missing").exists())


if __name__ == "__main__":
    unittest.main()
