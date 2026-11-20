"""Small synthetic statistics fixtures; never read real experiment results."""

import contextlib
import csv
import importlib.util
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("bootstrap", ROOT / "experiments/analysis/bootstrap.py")
bootstrap = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = bootstrap
SPEC.loader.exec_module(bootstrap)


def run(seed, mode, values, topology=None, **kwargs):
    values = np.asarray(values, dtype=float)
    return bootstrap.Run("fixture", .5, seed, mode, topology or f"topology-{seed}", f"flows-{seed}",
                         {"small": bootstrap.empirical_quantile(values, .99)}, {"small": len(values)},
                         values={"small": values}, **kwargs)


def paired(values=(10, 20, 80), scale=2):
    return [run(seed, mode, [value * factor]) for seed, value in enumerate(values, 1)
            for mode, factor in (("none", 1), ("shortest", scale))]


def analyze(runs, **kwargs):
    return bootstrap.analyze(runs, modes=("shortest",), replicates=300, **kwargs)


class BootstrapTests(unittest.TestCase):
    def test_pairing_preserves_exact_ratio(self):
        rows, _ = analyze(paired())
        ratio = rows[2]
        self.assertAlmostEqual(rows[0]["estimate"], 110 / 3)
        self.assertEqual((ratio["estimate"], ratio["ci_low"], ratio["ci_high"]), (2, 2, 2))
        self.assertIn("exploratory_few_clusters", ratio["status"])
        self.assertGreater(rows[0]["ci_high"], rows[0]["ci_low"])

    def test_ratio_of_means_is_not_mean_of_ratios(self):
        runs = [run(1, "none", [1]), run(1, "shortest", [2]),
                run(2, "none", [10]), run(2, "shortest", [10])]
        rows, _ = analyze(runs)
        self.assertAlmostEqual(rows[2]["estimate"], 12 / 11)
        self.assertNotAlmostEqual(rows[2]["estimate"], 1.5)

    def test_order_and_rng_reproducibility(self):
        first, first_pairs = analyze(paired(), seed=4)
        second, second_pairs = analyze(list(reversed(paired())), seed=4)
        self.assertEqual(first, second)
        self.assertEqual(first_pairs, second_pairs)
        weights = bootstrap.bootstrap_weights(["a", "b", "c"], 200, 4)
        np.testing.assert_array_equal(weights, bootstrap.bootstrap_weights(["a", "b", "c"], 200, 4))
        self.assertFalse(np.array_equal(weights, bootstrap.bootstrap_weights(["a", "b", "c"], 200, 5)))

    def test_shared_topology_is_one_cluster_even_with_multiple_seeds(self):
        runs = paired()
        for item in runs:
            item.topology = "shared"
        rows, _ = analyze(runs)
        self.assertEqual(rows[0]["paired_runs"], 3)
        self.assertEqual(rows[0]["clusters"], 1)
        self.assertTrue(math.isnan(rows[0]["ci_low"]))
        self.assertIn("withheld_one_cluster", rows[0]["status"])

    def test_repeated_runs_share_seed_cluster_and_are_run_weighted(self):
        runs = paired(values=(2, 8))
        runs.extend([run(1, "none", [20], run_id="repeat"), run(1, "shortest", [40], run_id="repeat")])
        rows, _ = analyze(runs)
        self.assertEqual(rows[0]["clusters"], 2)
        self.assertEqual(rows[0]["paired_runs"], 3)
        self.assertEqual(rows[0]["estimate"], 10)
        self.assertEqual(rows[2]["ci_low"], 2)

    def test_shared_topology_transitively_merges_seeds_across_conditions(self):
        runs = paired()
        extra = [run(1, "none", [30], topology="topology-2"),
                 run(1, "shortest", [60], topology="topology-2")]
        for item in extra:
            item.load = .7
        runs += extra
        clusters = bootstrap.cluster_ids(runs)
        self.assertEqual(clusters[(*runs[0].key, "none")], clusters[(*runs[2].key, "none")])
        rows, _ = analyze(runs)
        self.assertEqual(rows[0]["clusters"], 2)

    def test_missing_pairs_fail_or_are_explicitly_excluded(self):
        runs = paired()[:-1]
        with self.assertRaisesRegex(ValueError, "Incomplete pairing"):
            analyze(runs)
        rows, metadata = analyze(runs, allow_incomplete=True)
        self.assertEqual(rows[0]["paired_runs"], 2)
        self.assertEqual(metadata["excluded_pairs"][0]["missing_modes"], ["shortest"])
        with self.assertRaisesRegex(ValueError, "Incomplete pairing"):
            analyze(paired(), expected_seeds=[1, 2, 3, 4])

    def test_duplicate_and_mismatched_pairs_fail_even_when_incomplete_allowed(self):
        with self.assertRaisesRegex(ValueError, "Duplicate run"):
            analyze(paired() + paired()[:1])
        for field, value in (("fingerprint", "different"), ("topology", "different"),
                             ("config", "different"), ("counts", {"small": 4})):
            with self.subTest(field=field):
                runs = paired()
                setattr(runs[1], field, value)
                with self.assertRaisesRegex(ValueError, "mismatch"):
                    analyze(runs, allow_incomplete=True)

    def test_pooled_and_mean_run_estimands_differ(self):
        runs = [run(1, "none", [1] * 100), run(1, "shortest", [2] * 100),
                run(2, "none", [100]), run(2, "shortest", [200])]
        mean, _ = analyze(runs)
        pooled, _ = analyze(runs, estimand="pooled-p99")
        self.assertEqual(mean[0]["estimate"], 50.5)
        self.assertEqual(pooled[0]["estimate"], 1)
        self.assertEqual(pooled[2]["estimate"], 2)
        # The weighted order statistic agrees with literal cluster duplication.
        evaluator = bootstrap.statistic([runs[0], runs[2]], ["a", "b"], ["a", "b"], "small", "pooled-p99")
        for weights in ([1, 1], [2, 0], [0, 2], [1, 2]):
            expected = bootstrap.empirical_quantile([1] * (100 * weights[0]) + [100] * weights[1], .99)
            self.assertEqual(evaluator(np.array(weights)), expected)

    def test_infinite_order_statistics_and_undefined_ratios(self):
        self.assertEqual(bootstrap.empirical_quantile([1] * 99 + [np.inf], .99), 1)
        self.assertTrue(math.isinf(bootstrap.empirical_quantile([1] * 98 + [np.inf] * 2, .99)))
        runs = paired(values=(1, np.inf))
        rows, _ = analyze(runs)
        self.assertTrue(math.isinf(rows[0]["ci_high"]))
        self.assertTrue(math.isnan(rows[2]["estimate"]))
        self.assertTrue(math.isnan(rows[2]["ci_low"]))
        self.assertGreater(rows[2]["undefined_replicates"], 0)
        self.assertIn("withheld_undefined_statistic", rows[2]["status"])
        self.assertTrue(math.isnan(bootstrap.ratio(1, np.inf)))
        self.assertTrue(math.isnan(bootstrap.ratio(1, 0)))

    def test_undefined_draws_are_not_discarded_even_if_point_is_defined(self):
        runs = [run(1, "none", [0]), run(1, "shortest", [1]),
                run(2, "none", [2]), run(2, "shortest", [2])]
        rows, _ = analyze(runs)
        self.assertEqual(rows[2]["estimate"], 1.5)
        self.assertTrue(math.isnan(rows[2]["ci_low"]))
        self.assertGreater(rows[2]["undefined_replicates"], 0)

    def test_empty_class_and_missing_raw_arrays_fail(self):
        runs = paired()
        for item in runs[:2]:
            item.counts["small"] = 0
        with self.assertRaisesRegex(ValueError, "Empty small"):
            analyze(runs)
        runs = paired()
        runs[0].values = {}
        with self.assertRaisesRegex(ValueError, "requires raw"):
            analyze(runs, estimand="pooled-p99")


class InputTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="bootstrap-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def raw_run(self, mode="none", seed=1, suffix=""):
        directory = self.root / "batch" / ("03-load-sweep-baseline" if mode == "none" else "03-load-sweep-bypass") / f"{mode}-{seed}{suffix}"
        directory.mkdir(parents=True)
        (directory / "args.txt").write_text(f"--load=0.5 --seed={seed} --bypass={mode}\n")
        (directory / "meta.txt").write_text("commit: fixture\nexit_status: 0\n")
        (directory / "summary.txt").write_text("completed=3/4 sim_end_us=100000\n")
        with (directory / "flows.csv").open("w", newline="") as stream:
            writer = csv.writer(stream)
            writer.writerow([*bootstrap.IDENTITY, "finish_ns"])
            # Middle 80% retains starts 200 and 800; unfinished is retained as inf.
            for index, start in enumerate([0, 200, 800, 1000]):
                writer.writerow([index, 0, 1, 1, 1, 10000, start, "" if index == 2 else start + 20000000])
        (directory / "links.csv").write_text("from,to,port_kind\n0,1,fabric\n1,0,fabric\n")
        return directory

    def csv_input(self):
        source = self.root / "source" / "runs.csv"
        source.parent.mkdir()
        with source.open("w", newline="") as stream:
            fields = ["scenario", "load", "seed", "bypass", "topology_fingerprint", "flow_fingerprint",
                      "small_flows", "small_p99_us", "unfinished"]
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            for item in paired():
                writer.writerow(dict(scenario=item.scenario, load=item.load, seed=item.seed, bypass=item.mode,
                                     topology_fingerprint=item.topology, flow_fingerprint=item.fingerprint,
                                     small_flows=item.counts["small"], small_p99_us=item.p99["small"], unfinished=0))
        return source

    def test_raw_trimming_unfinished_and_non_timeout_filtering(self):
        self.raw_run()
        self.raw_run("shortest")
        partial = self.raw_run("none", seed=2, suffix=".partial")
        failed = self.raw_run("none", seed=3, suffix=".failed")
        runs, metadata = bootstrap.load_batch(self.root / "batch", ["small"])
        self.assertEqual(len(runs), 2)
        self.assertEqual(set(metadata["ignored_directories"]), {str(partial), str(failed)})
        self.assertEqual(runs[0].counts["small"], 2)
        self.assertEqual(runs[0].unfinished, 1)
        self.assertEqual(runs[0].values["small"][0], 20000)
        self.assertTrue(math.isinf(runs[0].values["small"][1]))
        analyze(runs)

    def test_raw_bad_completion_and_failed_status_rejected(self):
        directory = self.raw_run()
        (directory / "summary.txt").write_text("completed=4/4\n")
        with self.assertRaisesRegex(ValueError, "completion counts"):
            bootstrap.load_batch(self.root / "batch", ["small"])
        (directory / "meta.txt").write_text("exit_status: 1\n")
        with self.assertRaisesRegex(ValueError, "Nonzero exit"):
            bootstrap.load_batch(self.root / "batch", ["small"])

    def test_raw_bad_bytes_duplicate_ids_and_incomplete_final_directories_fail(self):
        directory = self.raw_run()
        original = (directory / "flows.csv").read_text()
        for field, replacement, error in (("bytes", "0", "Invalid flow"), ("bytes", "-5", "Invalid flow"),
                                           ("flow", "-1", "Invalid flow"), ("flow", "1", "duplicate flow")):
            with self.subTest(field=field, replacement=replacement):
                lines = original.splitlines()
                row = lines[1].split(",")
                row[lines[0].split(",").index(field)] = replacement
                lines[1] = ",".join(row)
                (directory / "flows.csv").write_text("\n".join(lines) + "\n")
                with self.assertRaisesRegex(ValueError, error):
                    bootstrap.load_batch(self.root / "batch", ["small"])
        (directory / "flows.csv").write_text(original)
        (directory / "summary.txt").unlink()
        with self.assertRaises(FileNotFoundError):
            bootstrap.load_batch(self.root / "batch", ["small"])

    def test_raw_reordered_flows_still_pair_and_config_mismatches_fail(self):
        self.raw_run()
        other = self.raw_run("shortest")
        lines = (other / "flows.csv").read_text().splitlines()
        (other / "flows.csv").write_text("\n".join([lines[0], *reversed(lines[1:])]) + "\n")
        runs, _ = bootstrap.load_batch(self.root / "batch", ["small"])
        analyze(runs)
        with (other / "args.txt").open("a") as stream:
            stream.write("--minRtoUs=2000\n")
        runs, _ = bootstrap.load_batch(self.root / "batch", ["small"])
        with self.assertRaisesRegex(ValueError, "config mismatch"):
            analyze(runs)

    def test_raw_different_configurations_across_seed_pairs_fail(self):
        for seed in (1, 2):
            for mode in ("none", "shortest"):
                directory = self.raw_run(mode, seed=seed)
                if seed == 2:
                    with (directory / "args.txt").open("a") as stream:
                        stream.write("--minRtoUs=2000\n")
        runs, _ = bootstrap.load_batch(self.root / "batch", ["small"])
        with self.assertRaisesRegex(ValueError, "Mixed run configurations"):
            analyze(runs)

    def test_csv_cli_writes_separate_reproducible_strict_json(self):
        source = self.csv_input()
        before = source.read_bytes()
        output = self.root / "output"
        args = ["--runs-csv", str(source), "--output", str(output), "--mode", "shortest", "--replicates", "100"]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(bootstrap.main(args), 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertEqual(manifest["results"][2]["estimate"], 2)
        self.assertEqual(manifest["configuration"]["estimand"], "mean-run-p99")
        self.assertEqual(source.read_bytes(), before)
        self.assertTrue((output / "intervals.csv").is_file())
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            bootstrap.main(args)  # no silent overwrite
        self.assertEqual(bootstrap.json_safe({"infinity": np.inf, "undefined": np.nan}),
                         {"infinity": "inf", "undefined": None})

    def test_cli_rejects_output_inside_source_and_pooled_csv(self):
        source = self.csv_input()
        for extra, output in (([], source.parent / "analysis"), (["--estimand", "pooled-p99"], self.root / "output")):
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                bootstrap.main(["--runs-csv", str(source), "--output", str(output), *extra])
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
