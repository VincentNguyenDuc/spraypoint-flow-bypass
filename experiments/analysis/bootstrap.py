#!/usr/bin/env python3
"""Paired cluster bootstrap for H2 p99s; see STATISTICS.md for estimands and limits."""

import argparse
import csv
from dataclasses import dataclass, field
import hashlib
import json
import math
from pathlib import Path
import platform
import re
import shlex
import sys

import numpy as np


CLASSES = ("all", "small", "small_adjacent", "small_other", "large")
MODES = ("none", "adjacent", "shortest")
IDENTITY = ("flow", "source_router", "destination_router", "adjacent", "small", "bytes", "start_ns")


@dataclass
class Run:
    scenario: str
    load: float
    seed: int
    mode: str
    topology: str
    fingerprint: str
    p99: dict
    counts: dict
    run_id: str = "0"
    values: dict = field(default_factory=dict, repr=False)
    config: str = ""
    unfinished: int = 0
    source: str = ""

    @property
    def key(self):
        return self.scenario, self.load, self.seed, self.run_id


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def empirical_quantile(values, q):
    """Inverse empirical CDF, including +inf; never interpolate across infinities."""
    values = np.asarray(values, dtype=float)
    if not len(values) or np.isnan(values).any():
        return float("nan")
    return float(np.quantile(values, q, method="inverted_cdf"))


def ratio(numerator, denominator):
    # Infinite FCT is a censoring sentinel, so even finite/inf is not a measured ratio.
    if not math.isfinite(numerator) or not math.isfinite(denominator) or denominator <= 0:
        return float("nan")
    return numerator / denominator


def _positive_metric(value, name, allow_empty=False):
    number = float(value) if value not in ("", None) else float("nan")
    if number < 0 or (math.isnan(number) and not allow_empty):
        raise ValueError(f"Invalid {name}: {value!r}")
    return number


def _stamp(path):
    stat = path.stat()
    return [stat.st_size, stat.st_mtime_ns]


def load_csv(path, classes):
    """Read the existing h2-runs.csv schema (optional run_id for repeated runs)."""
    before = _stamp(path)
    data = path.read_bytes()
    rows = list(csv.DictReader(data.decode().splitlines()))
    if _stamp(path) != before:
        raise ValueError(f"Input changed while reading: {path}; use a stable CSV copy")
    runs = []
    for row in rows:
        counts = {name: int(row[f"{name}_flows"]) for name in classes}
        if any(n < 0 for n in counts.values()):
            raise ValueError("Flow counts cannot be negative")
        p99 = {name: _positive_metric(row[f"{name}_p99_us"], f"{name}_p99_us", counts[name] == 0)
               for name in classes}
        runs.append(Run(row["scenario"], float(row["load"]), int(row["seed"]), row["bypass"],
                        row["topology_fingerprint"], row["flow_fingerprint"], p99, counts,
                        run_id=row.get("run_id", "0"), unfinished=int(row["unfinished"]), source=str(path)))
    return runs, {"kind": "h2-runs.csv", "path": str(path), "sha256": hashlib.sha256(data).hexdigest(),
                  "stamp": before, "ignored_directories": [],
                  "caveat": "Aggregate inputs trust upstream p99 calculation, trimming, completion, and run settings."}


def _scenario(directory):
    match = re.fullmatch(r"(03-load-sweep|04-h2-(.+))-(baseline|bypass)", directory.name)
    return ("default" if match[1] == "03-load-sweep" else match[2]) if match else None


def _read_run(directory, scenario, classes, trim):
    names = ("args.txt", "meta.txt", "summary.txt", "flows.csv", "links.csv")
    before = {name: _stamp(directory / name) for name in names}
    options = dict(arg.removeprefix("--").split("=", 1)
                   for arg in shlex.split((directory / "args.txt").read_text()))
    meta = dict(line.split(": ", 1) for line in (directory / "meta.txt").read_text().splitlines())
    if meta.get("exit_status", "0") != "0":
        raise ValueError(f"Nonzero exit status: {directory}")
    summary = (directory / "summary.txt").read_text()
    completed = re.search(r"^completed=(\d+)/(\d+)\b", summary, re.M)
    if not completed:
        raise ValueError(f"Missing completion counts: {directory}")
    # Read only the identity and FCT data, without creating timeout classifications.
    identities, fcts = [], []
    with (directory / "flows.csv").open(newline="") as stream:
        for row in csv.DictReader(stream):
            identity = tuple(int(row[name]) for name in IDENTITY)
            finish = row["finish_ns"]
            fct = (int(finish) - identity[-1]) / 1000 if finish else float("inf")
            if (fct < 0 or identity[-1] < 0 or any(value < 0 for value in identity[:3])
                    or identity[5] <= 0 or identity[3] not in (0, 1) or identity[4] not in (0, 1)):
                raise ValueError(f"Invalid flow identity/bytes/timing/classification: {directory}")
            identities.append(identity)
            fcts.append(fct)
    if len(identities) != int(completed[2]) or sum(math.isfinite(x) for x in fcts) != int(completed[1]):
        raise ValueError(f"Flow rows disagree with summary completion counts: {directory}")
    if not identities or len({row[0] for row in identities}) != len(identities):
        raise ValueError(f"Empty or duplicate flow identities: {directory}")
    # Match by flow id, even if a CSV was reordered.
    order = sorted(range(len(identities)), key=lambda index: identities[index][0])
    fingerprint = digest([identities[index] for index in order])
    last = max(row[-1] for row in identities)
    selected = {name: [] for name in classes}
    for row, fct in zip(identities, fcts):
        if not trim * last <= row[-1] <= (1 - trim) * last:
            continue
        adjacent, small = row[3:5]
        membership = {"all", "small" if small else "large"}
        if small:
            membership.add("small_adjacent" if adjacent else "small_other")
        for name in membership.intersection(classes):
            selected[name].append(fct)
    with (directory / "links.csv").open(newline="") as stream:
        links = csv.DictReader(stream)
        if "port_kind" not in (links.fieldnames or []):
            raise ValueError(f"Missing port_kind in links.csv: {directory}")
        edges = sorted((int(row["from"]), int(row["to"])) for row in links if row["port_kind"] == "fabric")
    if not edges or len(edges) != len(set(edges)):
        raise ValueError(f"Empty or duplicate fabric edges: {directory}")
    topology = digest(edges)
    config = digest({"options": {k: v for k, v in options.items() if k not in ("seed", "bypass", "outputDir")},
                     "commit": meta.get("commit", "")})
    after = {name: _stamp(directory / name) for name in names}
    if before != after:
        raise ValueError(f"Input changed while reading: {directory}")
    run = Run(scenario, float(options["load"]), int(options["seed"]), options.get("bypass", "none"),
              topology, fingerprint, {name: empirical_quantile(values, .99) for name, values in selected.items()},
              {name: len(values) for name, values in selected.items()},
              values={name: np.asarray(values) for name, values in selected.items()}, config=config,
              unfinished=int(completed[2]) - int(completed[1]), source=str(directory))
    return run, {"path": str(directory), "files": before}


def load_batch(path, classes, trim=.1, scenarios=(), loads=()):
    runs, inputs, ignored = [], [], []
    for experiment in sorted(path.iterdir()):
        scenario = _scenario(experiment)
        if not experiment.is_dir() or scenario is None or (scenarios and scenario not in scenarios):
            continue
        for directory in sorted(experiment.iterdir()):
            if not directory.is_dir():
                continue
            if directory.suffix in (".partial", ".failed"):
                ignored.append(str(directory))
                continue
            if loads:
                args = dict(arg.removeprefix("--").split("=", 1)
                            for arg in shlex.split((directory / "args.txt").read_text()))
                if float(args["load"]) not in loads:
                    continue
            run, evidence = _read_run(directory, scenario, classes, trim)
            runs.append(run)
            inputs.append(evidence)
    return runs, {"kind": "completed-run-directories", "path": str(path), "trim_fraction": trim,
                  "inputs": inputs, "ignored_directories": ignored,
                  "caveat": "Final directories and completion counts checked; this is not a routing/drop-health audit."}


def validate_runs(runs):
    seen = set()
    for run in runs:
        if run.mode not in MODES or not run.scenario or not math.isfinite(run.load) or run.seed < 0:
            raise ValueError(f"Invalid run key: {run.key}, {run.mode}")
        if not run.topology or not run.fingerprint or not run.run_id or run.unfinished < 0:
            raise ValueError(f"Missing fingerprint/identity or negative unfinished count: {run.key}")
        key = (*run.key, run.mode)
        if key in seen:
            raise ValueError(f"Duplicate run: {key}")
        seen.add(key)


def cluster_ids(runs):
    """Connected components of shared seed OR topology; never split reused graphs."""
    parent = {}

    def root(node):
        parent.setdefault(node, node)
        while parent[node] != node:
            parent[node] = parent[parent[node]]
            node = parent[node]
        return node

    for run in runs:
        left, right = root(f"seed:{run.seed}"), root(f"topology:{run.topology}")
        parent[max(left, right)] = min(left, right)
    members = {}
    for node in parent:
        members.setdefault(root(node), []).append(node)
    labels = {node: digest(sorted(members[root(node)])) for node in parent}
    return {(*run.key, run.mode): labels[f"seed:{run.seed}"] for run in runs}


def paired_groups(runs, modes, allow_incomplete=False, expected_seeds=()):
    validate_runs(runs)
    indexed = {(*run.key, run.mode): run for run in runs if run.mode in ("none", *modes)}
    conditions = sorted({(run.scenario, run.load) for run in indexed.values()})
    pairs, excluded = [], []
    for scenario, load in conditions:
        keys = {key[:4] for key in indexed if key[:2] == (scenario, load)}
        for seed in expected_seeds:
            if not any(key[2] == seed for key in keys):
                keys.add((scenario, load, seed, "0"))
        for mode in modes:
            matched = []
            for key in sorted(keys):
                baseline, other = indexed.get((*key, "none")), indexed.get((*key, mode))
                if baseline is None or other is None:
                    excluded.append({"key": list(key), "comparison": mode,
                                     "missing_modes": [m for m, run in (("none", baseline), (mode, other)) if run is None]})
                    continue
                for field in ("fingerprint", "topology", "config", "counts"):
                    if getattr(baseline, field) != getattr(other, field):
                        raise ValueError(f"Paired {field} mismatch for {key}, {mode}")
                matched.append((baseline, other))
            if matched:
                if len({baseline.config for baseline, _ in matched}) != 1:
                    raise ValueError(f"Mixed run configurations within {scenario}, load {load}, {mode}")
                pairs.append((scenario, load, mode, matched))
    if excluded and not allow_incomplete:
        raise ValueError(f"Incomplete pairing ({len(excluded)} missing comparisons); first: {excluded[0]}. "
                         "Use --allow-incomplete to explicitly report and exclude unmatched runs.")
    if not pairs:
        raise ValueError("No matched baseline/bypass runs")
    return pairs, excluded


def bootstrap_weights(labels, replicates, seed):
    # The same cohort gets the same draws across modes/classes/loads/scenarios.
    entropy = [seed, *np.frombuffer(hashlib.sha256("\n".join(labels).encode()).digest(), dtype="<u4").tolist()]
    rng = np.random.Generator(np.random.PCG64(np.random.SeedSequence(entropy)))
    n = len(labels)
    return rng.multinomial(n, np.full(n, 1 / n), size=replicates)


def statistic(runs, run_clusters, labels, name, estimand):
    positions = {label: index for index, label in enumerate(labels)}
    ids = np.array([positions[label] for label in run_clusters])
    if estimand == "mean-run-p99":
        values = np.array([run.p99[name] for run in runs])

        def evaluate(weights):
            multiplicity = weights[ids]
            present = multiplicity > 0  # avoid 0 * infinity
            return float(np.sum(values[present] * multiplicity[present]) / multiplicity.sum())
    else:
        values = np.concatenate([run.values[name] for run in runs])
        owners = np.repeat(ids, [run.counts[name] for run in runs])
        order = np.argsort(values)
        values, owners = values[order], owners[order]

        def evaluate(weights):
            cumulative = np.cumsum(weights[owners])
            # Exact integer rank for p99; duplication of clusters never duplicates flow arrays.
            rank = (99 * int(cumulative[-1]) + 99) // 100
            return float(values[np.searchsorted(cumulative, rank)])
    return evaluate


def interval(point, distribution, clusters, confidence):
    invalid = int(np.isnan(distribution).sum())
    flags = []
    if clusters < 2:
        flags.append("withheld_one_cluster")
    elif clusters < 10:
        flags.append("exploratory_few_clusters")
    if invalid or math.isnan(point):
        flags.append("withheld_undefined_statistic")
    if math.isinf(point) or np.isinf(distribution).any():
        flags.append("infinite_fct_sentinel")
    low = high = float("nan")
    if clusters >= 2 and not invalid and not math.isnan(point):
        tail = (1 - confidence) / 2
        low, high = (empirical_quantile(distribution, q) for q in (tail, 1 - tail))
        if low == high:
            flags.append("degenerate_empirical_interval")
    return {"estimate": point, "ci_low": low, "ci_high": high, "undefined_replicates": invalid,
            "infinite_replicates": int(np.isinf(distribution).sum()), "status": ";".join(flags) or "percentile_interval"}


def analyze(runs, classes=("small",), modes=("adjacent", "shortest"), estimand="mean-run-p99",
            replicates=10000, seed=561, confidence=.95, allow_incomplete=False, expected_seeds=()):
    if replicates < 1 or seed < 0 or not 0 < confidence < 1:
        raise ValueError("Need positive replicates, nonnegative RNG seed, and 0 < confidence < 1")
    if estimand not in ("mean-run-p99", "pooled-p99") or not classes or not modes:
        raise ValueError("Invalid estimand or empty classes/modes")
    if any(mode not in MODES[1:] for mode in modes) or any(name not in CLASSES for name in classes):
        raise ValueError("Invalid mode or flow class")
    groups, excluded = paired_groups(runs, modes, allow_incomplete, expected_seeds)
    clusters = cluster_ids(runs)
    results, cohorts = [], []
    for scenario, load, mode, pairs in groups:
        labels_per_run = [clusters[(*baseline.key, baseline.mode)] for baseline, _ in pairs]
        labels = sorted(set(labels_per_run))
        weights = bootstrap_weights(labels, replicates, seed)
        unique, inverse = np.unique(weights, axis=0, return_inverse=True)
        cohorts.append({"scenario": scenario, "load": load, "mode": mode,
                        "pairs": [{"key": list(baseline.key), "cluster": cluster,
                                   "baseline_source": baseline.source, "bypass_source": other.source}
                                  for (baseline, other), cluster in zip(pairs, labels_per_run)]})
        for name in classes:
            if any(baseline.counts[name] == 0 for baseline, _ in pairs):
                raise ValueError(f"Empty {name} population in {scenario}, load {load}, {mode}; select another class")
            if estimand == "pooled-p99" and any(name not in run.values for pair in pairs for run in pair):
                raise ValueError("Pooled-flow p99 requires raw --batch inputs; per-run quantiles cannot reconstruct it")
            evaluators = [statistic([pair[i] for pair in pairs], labels_per_run, labels, name, estimand) for i in (0, 1)]
            points = [evaluate(np.ones(len(labels), dtype=int)) for evaluate in evaluators]
            distributions = [np.array([evaluate(weight) for weight in unique])[inverse] for evaluate in evaluators]
            ratio_distribution = np.array([ratio(other, baseline) for baseline, other in zip(*distributions)])
            common = {"scenario": scenario, "load": load, "bypass": mode, "class": name, "estimand": estimand,
                      "paired_runs": len(pairs), "seeds": len({pair[0].seed for pair in pairs}),
                      "clusters": len(labels), "analyzed_flows_per_mode": sum(pair[0].counts[name] for pair in pairs),
                      "min_run_beyond_p99": min(pair[0].counts[name] // 100 for pair in pairs),
                      "unfinished_full_runs_baseline": sum(pair[0].unfinished for pair in pairs),
                      "unfinished_full_runs_bypass": sum(pair[1].unfinished for pair in pairs),
                      "replicates": replicates, "unique_resamples": len(unique), "confidence": confidence}
            for metric, point, distribution in zip(("baseline_p99_us", "bypass_p99_us", "bypass_over_baseline"),
                                                    [*points, ratio(points[1], points[0])], [*distributions, ratio_distribution]):
                results.append({**common, "metric": metric, **interval(point, distribution, len(labels), confidence)})
    return results, {"excluded_pairs": excluded, "cohorts": cohorts}


def json_safe(value):
    if isinstance(value, float) and not math.isfinite(value):
        return None if math.isnan(value) else ("inf" if value > 0 else "-inf")
    if isinstance(value, dict):
        return {key: json_safe(item) for key, item in value.items()}
    if isinstance(value, (list, tuple)):
        return [json_safe(item) for item in value]
    return value


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--batch", type=Path, help="H2 batch with finalized run directories")
    source.add_argument("--runs-csv", type=Path, help="Stable copy of h2-runs.csv")
    parser.add_argument("--output", type=Path, required=True, help="New directory outside the input directory")
    parser.add_argument("--class", dest="classes", action="append", choices=CLASSES)
    parser.add_argument("--mode", dest="modes", action="append", choices=MODES[1:])
    parser.add_argument("--scenario", action="append", default=[])
    parser.add_argument("--load", type=float, action="append", default=[])
    parser.add_argument("--estimand", choices=("mean-run-p99", "pooled-p99"), default="mean-run-p99")
    parser.add_argument("--replicates", type=int, default=10000)
    parser.add_argument("--rng-seed", type=int, default=561)
    parser.add_argument("--confidence", type=float, default=.95)
    parser.add_argument("--trim", type=float, default=.1, help="Raw batch only: trim this fraction from each arrival-window end")
    parser.add_argument("--allow-incomplete", action="store_true")
    parser.add_argument("--expected-seeds", type=int, nargs="+", default=[])
    args = parser.parse_args(argv)
    try:
        source_path = (args.batch or args.runs_csv).resolve()
        input_directory = source_path if args.batch else source_path.parent
        output = args.output.resolve()
        if output == input_directory or input_directory in output.parents or output.exists():
            raise ValueError("--output must be a new directory outside the input directory")
        if not 0 <= args.trim < .5:
            raise ValueError("Need 0 <= trim < 0.5")
        if args.runs_csv and (args.estimand == "pooled-p99" or args.trim != .1):
            raise ValueError("CSV input supports mean-run-p99 with its upstream trim only; use --batch for raw-flow analysis")
        classes, modes = list(dict.fromkeys(args.classes or ["small"])), list(dict.fromkeys(args.modes or MODES[1:]))
        if args.batch:
            runs, provenance = load_batch(source_path, classes, args.trim, args.scenario, args.load)
        else:
            runs, provenance = load_csv(source_path, classes)
        runs = [run for run in runs if (not args.scenario or run.scenario in args.scenario)
                and (not args.load or run.load in args.load)]
        results, pairing = analyze(runs, classes, modes, args.estimand, args.replicates, args.rng_seed,
                                   args.confidence, args.allow_incomplete, args.expected_seeds)
        manifest = {"schema_version": 1, "input": provenance, "pairing": pairing,
                    "configuration": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
                    "numpy_version": np.__version__, "python_version": platform.python_version(),
                    "rng": "PCG64/SeedSequence; stable cohort hash; multinomial cluster counts",
                    "interval": "pointwise percentile, inverse empirical CDF endpoints",
                    "completeness": "Only observed conditions checked; expected-seeds checks supplied seeds within them. "
                                    "Absent conditions are not inferred. Exclusions can bias results.",
                    "limitations": "Whole paired seed/topology clusters, without within-run iid resampling. "
                                   "Fewer than ten clusters labeled exploratory (heuristic, not a coverage guarantee); "
                                   "one cluster has no interval. More replicates do not replace independent seeds. "
                                   "No timeout filtering or causal/crossover significance claim. "
                                   "Ratios involving infinite censoring sentinels are undefined.",
                    "results": results}
        encoded = json.dumps(json_safe(manifest), indent=2, allow_nan=False) + "\n"
        output.mkdir(parents=True, exist_ok=False)
        (output / "manifest.json").write_text(encoded)
        with (output / "intervals.csv").open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(results[0]))
            writer.writeheader()
            writer.writerows(json_safe(results))
    except (ValueError, KeyError, OSError) as error:
        parser.error(str(error))
    print(f"Wrote {len(results)} pointwise estimates to {output}; excluded pairs: {len(pairing['excluded_pairs'])}.")
    print("Read status and cluster counts: few-seed intervals are exploratory, not crossover confirmation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
