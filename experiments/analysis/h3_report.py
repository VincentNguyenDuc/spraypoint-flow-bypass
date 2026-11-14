#!/usr/bin/env python3
"""Describe matched H3 routing tradeoffs from a stable h2-runs.csv snapshot."""

import argparse
import csv
from dataclasses import dataclass
import hashlib
import io
import json
import math
from pathlib import Path
import platform
import re


MODES = ("none", "adjacent", "shortest")
CLASSES = ("all", "small", "small_adjacent", "small_other", "large")
COUNTS = ("flows", "analyzed_flows", *(f"{name}_flows" for name in CLASSES))
REQUIRED_METRICS = (
    "small_p50_us", "small_p99_us", "small_adjacent_p50_us", "small_adjacent_p99_us",
    "small_other_p50_us", "small_other_p99_us", "large_mean_goodput_gbps",
    "large_p01_goodput_gbps",
)
OPTIONAL_METRICS = (
    "large_p99_us", "mean_utilization", "max_utilization", "hotspot_ratio",
    "mean_busiest_neighbor_share", "full_queue_share", "mean_queue",
    "fabric_drops", "downlink_drops", "host_drops", "device_drops",
)
SHARES = ("small_byte_share", "adjacent_small_byte_share")
FINGERPRINTS = ("flow_fingerprint", "topology_fingerprint")
REQUIRED_COLUMNS = {
    "scenario", "load", "seed", "bypass", "unfinished", *COUNTS, *REQUIRED_METRICS,
    *SHARES, *FINGERPRINTS,
}
COMPARISONS = (("adjacent", "none"), ("shortest", "none"), ("shortest", "adjacent"))


@dataclass
class Run:
    raw: dict
    scenario: str
    load: float
    seed: int
    mode: str
    counts: dict
    metrics: dict
    shares: dict
    unfinished: int

    @property
    def key(self):
        return self.scenario, self.load, self.seed


def stamp(path):
    info = path.stat()
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def integer(value, field):
    if not re.fullmatch(r"[0-9]+", str(value).strip()):
        raise ValueError(f"{field} must be a nonnegative integer, got {value!r}")
    return int(value)


def metric(value, field):
    number = float(value) if value not in (None, "") else math.nan
    if number < 0:
        raise ValueError(f"{field} must not be negative, got {value!r}")
    return number


def load_csv(path):
    """Read once, reject concurrent replacement, and retain the exact input SHA."""
    path = Path(path).resolve()
    before = stamp(path)
    data = path.read_bytes()
    if stamp(path) != before or len(data) != before[2]:
        raise ValueError("Input changed while reading; use a stable h2-runs.csv snapshot")
    reader = csv.DictReader(io.StringIO(data.decode("utf-8-sig"), newline=""))
    columns = reader.fieldnames or []
    if len(columns) != len(set(columns)):
        raise ValueError("Duplicate CSV column names")
    missing = REQUIRED_COLUMNS - set(columns)
    if missing:
        raise ValueError("Missing required columns: " + ", ".join(sorted(missing)))
    metrics = (*REQUIRED_METRICS, *(name for name in OPTIONAL_METRICS if name in columns))
    runs, seen = [], set()
    for line, raw in enumerate(reader, 2):
        if None in raw or any(value is None for value in raw.values()):
            raise ValueError(f"CSV row {line} has the wrong number of fields")
        scenario = raw["scenario"].strip()
        load = float(raw["load"])
        seed = integer(raw["seed"], "seed")
        mode = raw["bypass"]
        if not scenario or not math.isfinite(load) or load <= 0 or mode not in MODES:
            raise ValueError(f"Invalid scenario/load/mode on CSV row {line}")
        key = (scenario, load, seed, mode)
        if key in seen:
            raise ValueError(f"Duplicate run key: {key}")
        seen.add(key)
        for field in FINGERPRINTS:
            if not re.fullmatch(r"[0-9a-fA-F]{64}", raw[field]):
                raise ValueError(f"{key}: {field} must be a SHA-256 hex digest")
            raw[field] = raw[field].lower()
        counts = {field: integer(raw[field], field) for field in COUNTS}
        unfinished = integer(raw["unfinished"], "unfinished")
        if not (0 < counts["flows"] and counts["analyzed_flows"] <= counts["flows"]
                and unfinished <= counts["flows"]
                and counts["all_flows"] == counts["analyzed_flows"]
                and counts["small_flows"] + counts["large_flows"] == counts["all_flows"]
                and counts["small_adjacent_flows"] + counts["small_other_flows"]
                == counts["small_flows"]):
            raise ValueError(f"{key}: inconsistent population counts")
        for name in CLASSES:
            field = f"{name}_beyond_p99"
            if field in raw and integer(raw[field], field) != counts[f"{name}_flows"] // 100:
                raise ValueError(f"{key}: {field} disagrees with population count")
        values = {field: metric(raw[field], field) for field in metrics}
        for name in ("small", "small_adjacent", "small_other"):
            if values[f"{name}_p50_us"] > values[f"{name}_p99_us"]:
                raise ValueError(f"{key}: {name} p50 exceeds p99")
            if counts[f"{name}_flows"] == 0 and any(
                    not math.isnan(values[f"{name}_p{q}_us"]) for q in (50, 99)):
                raise ValueError(f"{key}: nonempty quantile for empty {name} population")
        if counts["large_flows"] == 0 and any(
                not math.isnan(values[field]) for field in
                ("large_mean_goodput_gbps", "large_p01_goodput_gbps")):
            raise ValueError(f"{key}: nonempty goodput for empty large population")
        shares = {field: float(raw[field]) for field in SHARES}
        if (any(not math.isfinite(value) or not 0 <= value <= 1 for value in shares.values())
                or shares["adjacent_small_byte_share"] > shares["small_byte_share"]):
            raise ValueError(f"{key}: invalid small byte shares")
        runs.append(Run(raw, scenario, load, seed, mode, counts, values, shares, unfinished))
    if not runs:
        raise ValueError("Input CSV has no runs")
    return runs, {
        "path": str(path), "sha256": hashlib.sha256(data).hexdigest(),
        "stamp": before, "columns": columns, "metrics": list(metrics),
    }


def ratio(numerator, denominator):
    """Infinite FCT is a censoring sentinel, not a measured finite/inf ratio."""
    if not math.isfinite(numerator) or not math.isfinite(denominator):
        return math.nan, "undefined_nonfinite"
    if denominator <= 0:
        return math.nan, "undefined_zero_denominator"
    value = numerator / denominator
    return (value, "defined") if math.isfinite(value) else (math.nan, "undefined_overflow")


def change_pct(value):
    result = 100 * (value - 1)
    return result if math.isfinite(result) else math.nan


def key_fields(key):
    return dict(zip(("scenario", "load", "seed"), key))


def analyze(runs, allow_incomplete=False, expected_seeds=()):
    """Validate triples and return descriptive rows without touching the filesystem."""
    groups = {}
    for run in runs:
        group = groups.setdefault(run.key, {})
        if run.mode in group:
            raise ValueError(f"Duplicate run key: {(*run.key, run.mode)}")
        group[run.mode] = run
    conditions = {(run.scenario, run.load) for run in runs}
    for scenario, load in conditions:
        for seed in expected_seeds:
            groups.setdefault((scenario, load, seed), {})
    included, excluded = {}, []
    for key, group in sorted(groups.items()):
        present = list(group.values())
        if present:
            reference = present[0]
            for run in present[1:]:
                for field in FINGERPRINTS:
                    if run.raw[field] != reference.raw[field]:
                        raise ValueError(f"{key}: {field} mismatch across modes")
                if run.counts != reference.counts:
                    raise ValueError(f"{key}: population count mismatch across modes")
                if any(not math.isclose(run.shares[field], reference.shares[field],
                                        rel_tol=0, abs_tol=1e-12) for field in SHARES):
                    raise ValueError(f"{key}: byte-share mismatch across modes")
                if run.metrics.keys() != reference.metrics.keys():
                    raise ValueError(f"{key}: metric schema mismatch across modes")
        missing = [mode for mode in MODES if mode not in group]
        if missing:
            excluded.append({**key_fields(key),
                             "present_modes": ";".join(mode for mode in MODES if mode in group),
                             "missing_modes": ";".join(missing)})
        else:
            included[key] = group
    if excluded and not allow_incomplete:
        raise ValueError("Incomplete three-mode pairing: " + repr(excluded) +
                         "; --allow-incomplete explicitly excludes these entire triples")

    warnings, comparisons, frontiers, run_rows = [], [], [], []
    for key, group in sorted(groups.items()):
        for mode in MODES:
            if mode not in group:
                continue
            run = group[mode]
            row = {**run.raw, **run.counts, **run.metrics, **run.shares, **key_fields(key),
                   "unfinished": run.unfinished, "included": key in included}
            row["exclusion_reason"] = "" if key in included else "incomplete_three_mode_pairing"
            run_rows.append(row)
            def warn(code, detail):
                warnings.append({**key_fields(key), "bypass": mode, "code": code, "detail": detail})
            if run.unfinished:
                warn("unfinished_flows", f"{run.unfinished}/{run.counts['flows']} flows unfinished; "
                     "whole-run count, not restricted to the analyzed arrival window")
            for name in CLASSES[1:]:
                count = run.counts[f"{name}_flows"]
                if count // 100 < 100:
                    warn("small_tail_sample", f"{name}: n={count}, floor(n/100)={count // 100}; "
                         "fewer than 100 observations in a nominal 1% tail")
            for field, value in run.metrics.items():
                if not math.isfinite(value):
                    warn("nonfinite_metric", f"{field}={value}; dependent ratios are withheld")
    for item in excluded:
        warnings.append({**{name: item[name] for name in ("scenario", "load", "seed")},
                         "bypass": "", "code": "excluded_incomplete_triple",
                         "detail": f"missing modes: {item['missing_modes']}"})

    for key, group in sorted(included.items()):
        base = group["none"]
        small = base.counts["small_flows"]
        added_flow_share = base.counts["small_other_flows"] / small if small else math.nan
        added_byte_share = base.shares["small_byte_share"] - base.shares["adjacent_small_byte_share"]
        for numerator, denominator in COMPARISONS:
            row = {**key_fields(key), "comparison": f"{numerator}_vs_{denominator}",
                   "numerator_mode": numerator, "denominator_mode": denominator}
            for field in base.metrics:
                value, status = ratio(group[numerator].metrics[field], group[denominator].metrics[field])
                row[field + "_ratio"] = value
                row[field + "_ratio_status"] = status
                row[field + "_change_pct"] = change_pct(value)
            if denominator == "adjacent":
                row["incremental_small_flow_eligibility_share"] = added_flow_share
                row["incremental_total_byte_eligibility_share"] = added_byte_share
                row["small_p99_gain_pct"] = -row["small_p99_us_change_pct"]
                row["small_other_p99_gain_pct"] = -row["small_other_p99_us_change_pct"]
            comparisons.append(row)

        valid = all(run.counts["small_flows"] > 0 and run.counts["large_flows"] > 0
                    and math.isfinite(run.metrics["small_p99_us"])
                    and math.isfinite(run.metrics["large_mean_goodput_gbps"])
                    for run in group.values())
        for mode in MODES:
            run = group[mode]
            tail = run.metrics["small_p99_us"]
            goodput = run.metrics["large_mean_goodput_gbps"]
            dominators = []
            if valid:
                for other in MODES:
                    other_tail = group[other].metrics["small_p99_us"]
                    other_goodput = group[other].metrics["large_mean_goodput_gbps"]
                    if (other != mode and other_tail <= tail and other_goodput >= goodput
                            and (other_tail < tail or other_goodput > goodput)):
                        dominators.append(other)
            frontiers.append({**key_fields(key), "bypass": mode, "small_p99_us": tail,
                              "large_mean_goodput_gbps": goodput,
                              "on_frontier": not dominators if valid else "",
                              "dominated_by": ";".join(dominators),
                              "status": "exploratory_descriptive" if valid else
                              "withheld_nonfinite_or_empty_objective"})

    summaries = []
    buckets = {}
    for row in comparisons:
        key = row["scenario"], row["load"], row["comparison"]
        buckets.setdefault(key, []).append(row)
    for (scenario, load, comparison), rows in sorted(buckets.items()):
        metrics = [field.removesuffix("_ratio") for field in rows[0] if field.endswith("_ratio")]
        for field in metrics:
            values = [row[field + "_ratio"] for row in rows]
            defined = sum(math.isfinite(value) for value in values)
            complete = defined == len(values)
            mean = math.fsum(value / len(values) for value in values) if complete else math.nan
            summaries.append({"scenario": scenario, "load": load, "comparison": comparison,
                              "metric": field, "paired_seeds": len(rows), "defined_ratios": defined,
                              "mean_of_paired_ratios": mean,
                              "min_paired_ratio": min(values) if complete else math.nan,
                              "max_paired_ratio": max(values) if complete else math.nan,
                              "mean_paired_change_pct": change_pct(mean),
                              "status": "exploratory_descriptive" if complete else
                              "withheld_undefined_paired_ratio"})
    return dict(runs=run_rows, comparisons=comparisons, summary=summaries, pareto=frontiers,
                exclusions=excluded, warnings=warnings, included_triples=len(included))


def display(value):
    if isinstance(value, float):
        if math.isnan(value):
            return "undefined"
        if math.isinf(value):
            return "+inf"
        return f"{value:.6g}"
    return str(value).replace("|", "\\|").replace("\n", " ").replace("\r", " ")


def table(headers, rows):
    return ["| " + " | ".join(headers) + " |", "| " + " | ".join("---" for _ in headers) + " |",
            *("| " + " | ".join(display(value) for value in row) + " |" for row in rows)]


def markdown(result, provenance):
    lines = ["# H3 routing tradeoffs (exploratory)", "",
             f"Input: `{provenance['input']['path']}`.",
             f"Input SHA-256: `{provenance['input']['sha256']}`.",
             f"Validated input runs: {len(result['runs'])}. Complete three-mode groups: "
             f"{result['included_triples']}. Excluded groups: {len(result['exclusions'])}.", "",
             "All-small p50/p99 are the input's small_p50_us/small_p99_us, computed upstream "
             "over all selected small flows; no averaging of adjacent/non-adjacent quantiles. "
             "The H2 producer selects the middle 80% of arrivals. This report trusts those "
             "upstream quantiles, trimming, run settings, and completion bookkeeping.", "",
             "Ratios are numerator mode / denominator mode for each matched seed. The seed "
             "summary is the arithmetic mean of paired ratios, not a ratio of means or a "
             "pooled-flow quantile. Undefined ratios are retained; a seed-summary mean is "
             "withheld if any paired ratio is undefined. No confidence intervals, significance "
             "claims, crossover conclusions, or FCT-based timeout inference are produced.", "",
             "Small populations change across classification cutoffs. Scenario labels alone "
             "do not establish comparable populations; this report does not rank thresholds "
             "globally or identify an optimal cutoff. Pareto comparisons are restricted to "
             "the same scenario, load, and seed. Separate seeds are not pooled.", "",
             "## All-small and partition FCTs", ""]
    kept = [row for row in result["runs"] if row["included"]]
    lines += table(
        ["Scenario", "Load", "Seed", "Mode", "Small n", "Small p50 us", "Small p99 us",
         "Adjacent n", "Adjacent p50/p99 us", "Other n", "Other p50/p99 us", "Unfinished"],
        ([r["scenario"], r["load"], r["seed"], r["bypass"], r["small_flows"], r["small_p50_us"],
          r["small_p99_us"], r["small_adjacent_flows"],
          f"{r['small_adjacent_p50_us']} / {r['small_adjacent_p99_us']}", r["small_other_flows"],
          f"{r['small_other_p50_us']} / {r['small_other_p99_us']}", r["unfinished"]] for r in kept))
    lines += ["", "## Large-flow goodput and fabric concentration", "",
              "Slow-tail goodput is the per-flow 1st percentile (higher is better). "
              "Hotspot ratio is busiest/mean fabric-link utilization; busiest-neighbor share "
              "is the mean across receivers of the largest incoming neighbor's delivered-byte "
              "share. These are fabric concentration metrics, not host sender counts. "
              "Unavailable optional input metrics are shown as unavailable.", ""]
    lines += table(
        ["Scenario", "Load", "Seed", "Mode", "Large n", "Mean Gbps", "p01 Gbps",
         "Hotspot ratio", "Max utilization", "Mean busiest-neighbor share"],
        ([r["scenario"], r["load"], r["seed"], r["bypass"], r["large_flows"],
          r["large_mean_goodput_gbps"], r["large_p01_goodput_gbps"],
          r.get("hotspot_ratio", "unavailable"), r.get("max_utilization", "unavailable"),
          r.get("mean_busiest_neighbor_share", "unavailable")] for r in kept))
    lines += ["", "## Per-seed routing ratios", "",
              "FCT ratios below one and goodput ratios above one favor the numerator mode. "
              "Full metric ratios and status fields are in h3-comparisons.csv.", ""]
    lines += table(
        ["Scenario", "Load", "Seed", "Comparison", "Small p50", "Small p99", "Other p99",
         "Large mean goodput", "Large p01 goodput"],
        ([r["scenario"], r["load"], r["seed"], r["comparison"], r["small_p50_us_ratio"],
          r["small_p99_us_ratio"], r["small_other_p99_us_ratio"],
          r["large_mean_goodput_gbps_ratio"], r["large_p01_goodput_gbps_ratio"]]
         for r in result["comparisons"]))
    lines += ["", "## Shortest versus adjacent: incremental eligibility and gain", "",
              "Added small-flow eligibility is non-adjacent small flows / all analyzed small "
              "flows. Added byte eligibility is small_byte_share - adjacent_small_byte_share, "
              "using all generated bytes as the denominator. These describe policy eligibility, "
              "not measured route changes; the flow and byte windows differ. The aggregate "
              "cannot separate same-router flows from other non-adjacent flows, so eligibility "
              "can overstate additional fabric traffic when multiple hosts share a router. Gains compare "
              "shortest directly with adjacent; positive p99 gain means lower FCT.", ""]
    lines += table(
        ["Scenario", "Load", "Seed", "Added small-flow share", "Added total-byte share",
         "Small p99 gain %", "Other p99 gain %", "Large mean goodput change %"],
        ([r["scenario"], r["load"], r["seed"], r["incremental_small_flow_eligibility_share"],
          r["incremental_total_byte_eligibility_share"], r["small_p99_gain_pct"],
          r["small_other_p99_gain_pct"], r["large_mean_goodput_gbps_change_pct"]]
         for r in result["comparisons"] if r["comparison"] == "shortest_vs_adjacent"))
    lines += ["", "## Mean of paired ratios across seeds", "",
              "Every seed has equal weight. These are descriptive arithmetic means of "
              "per-seed ratios; individual seeds and sample-size warnings remain visible.", ""]
    lines += table(
        ["Scenario", "Load", "Comparison", "Metric", "Defined / paired seeds",
         "Mean of paired ratios", "Min / max paired ratio"],
        ([r["scenario"], r["load"], r["comparison"], r["metric"],
          f"{r['defined_ratios']} / {r['paired_seeds']}", r["mean_of_paired_ratios"],
          f"{display(r['min_paired_ratio'])} / {display(r['max_paired_ratio'])}"]
         for r in result["summary"] if r["metric"] in
         ("small_p99_us", "large_mean_goodput_gbps", "large_p01_goodput_gbps")))
    lines += ["", "## Descriptive Pareto frontier within each seed", "",
              "Objectives: lower all-small p99 and higher large mean goodput. A mode dominates "
              "another only if neither objective is worse and at least one is strictly better. "
              "Ties remain on the frontier. If any mode has a nonfinite or empty objective, "
              "the entire group's frontier is withheld. This is exploratory, not statistical "
              "evidence or a threshold recommendation.", ""]
    lines += table(
        ["Scenario", "Load", "Seed", "Mode", "Small p99 us", "Large mean Gbps",
         "On frontier", "Dominated by", "Status"],
        ([r[name] for name in ("scenario", "load", "seed", "bypass", "small_p99_us",
                               "large_mean_goodput_gbps", "on_frontier", "dominated_by", "status")]
         for r in result["pareto"]))
    lines += ["", "## Coverage, exclusions, and warnings", "",
              "Completeness is checked for every observed scenario/load/seed and any explicitly "
              "requested expected seeds. A CSV alone cannot reveal wholly absent scenarios, "
              "loads, or unrequested seeds; no claim of experiment-matrix completion is made.", ""]
    if result["exclusions"]:
        lines += table(["Scenario", "Load", "Seed", "Present modes", "Excluded missing modes"],
                       ([r[name] for name in ("scenario", "load", "seed", "present_modes", "missing_modes")]
                        for r in result["exclusions"]))
    else:
        lines.append("No incomplete observed/requested groups were excluded.")
    lines += ["", f"Warnings: {len(result['warnings'])}; all are recorded in h3-warnings.csv. "
              "The table below shows the first 20. A nominal 1% tail with fewer than 100 "
              "observations is flagged; this count is a sample-size diagnostic, not a "
              "count of values strictly greater than p99. Unfinished-flow counts cover the "
              "whole run and may differ by mode.", ""]
    lines += table(["Scenario", "Load", "Seed", "Mode", "Warning", "Detail"],
                   ([r[name] for name in ("scenario", "load", "seed", "bypass", "code", "detail")]
                    for r in result["warnings"][:20]))
    return "\n".join(lines) + "\n"


def write_csv(path, rows, empty_fields):
    fields = list(dict.fromkeys(field for row in rows for field in row)) if rows else empty_fields
    with path.open("x", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def report(source, output_dir, *, input_batch=None, allow_incomplete=False, expected_seeds=()):
    source, output_dir = Path(source).resolve(), Path(output_dir).resolve()
    inferred_batch = source.parent.parent if source.parent.name == "analysis" else source.parent
    input_batch = Path(input_batch).resolve() if input_batch is not None else inferred_batch
    if not source.is_relative_to(input_batch):
        raise ValueError("Input CSV must be inside --input-batch")
    if output_dir.is_relative_to(input_batch) or output_dir.is_relative_to(inferred_batch):
        raise ValueError("Output directory must be outside the input batch")
    if output_dir.exists():
        raise ValueError("Output directory must be new; existing reports are never overwritten")
    if not output_dir.parent.is_dir():
        raise ValueError("Output directory's parent must already exist")
    if any(seed < 0 for seed in expected_seeds) or len(set(expected_seeds)) != len(expected_seeds):
        raise ValueError("Expected seeds must be distinct nonnegative integers")
    runs, metadata = load_csv(source)
    result = analyze(runs, allow_incomplete=allow_incomplete, expected_seeds=expected_seeds)
    provenance = {
        "schema_version": 1, "input": metadata, "input_batch": str(input_batch),
        "output_dir": str(output_dir),
        "script_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "python_version": platform.python_version(), "allow_incomplete": allow_incomplete,
        "expected_seeds": list(expected_seeds), "input_runs": len(runs),
        "included_triples": result["included_triples"], "exclusions": result["exclusions"],
        "warning_count": len(result["warnings"]),
        "estimand": "arithmetic mean of per-seed paired metric ratios; no pooled quantiles",
        "scope": "exploratory within scenario/load/seed; no cross-cutoff optimality or significance claim",
    }
    content = markdown(result, provenance)
    if stamp(source) != metadata["stamp"]:
        raise ValueError("Input changed during analysis; use a stable h2-runs.csv snapshot")
    key_columns = ["scenario", "load", "seed"]
    schemas = {
        "runs": [*metadata["columns"], "included", "exclusion_reason"],
        "comparisons": [*key_columns, "comparison", "numerator_mode", "denominator_mode",
                        *(field + suffix for field in metadata["metrics"]
                          for suffix in ("_ratio", "_ratio_status", "_change_pct")),
                        "incremental_small_flow_eligibility_share",
                        "incremental_total_byte_eligibility_share", "small_p99_gain_pct",
                        "small_other_p99_gain_pct"],
        "summary": ["scenario", "load", "comparison", "metric", "paired_seeds", "defined_ratios",
                    "mean_of_paired_ratios", "min_paired_ratio", "max_paired_ratio",
                    "mean_paired_change_pct", "status"],
        "pareto": [*key_columns, "bypass", "small_p99_us", "large_mean_goodput_gbps",
                   "on_frontier", "dominated_by", "status"],
        "exclusions": [*key_columns, "present_modes", "missing_modes"],
        "warnings": [*key_columns, "bypass", "code", "detail"],
    }
    output_dir.mkdir()
    for name in ("runs", "comparisons", "summary", "pareto", "exclusions", "warnings"):
        write_csv(output_dir / f"h3-{name}.csv", result[name], schemas[name])
    (output_dir / "H3.md").write_text(content)
    (output_dir / "h3-provenance.json").write_text(json.dumps(provenance, indent=2, allow_nan=False) + "\n")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("runs_csv", type=Path, help="Stable aggregate h2-runs.csv snapshot")
    parser.add_argument("--output-dir", required=True, type=Path,
                        help="New directory outside the input batch; parent must exist")
    parser.add_argument("--input-batch", type=Path,
                        help="Batch root; default CSV parent, or parent of an analysis directory")
    parser.add_argument("--allow-incomplete", action="store_true",
                        help="Explicitly exclude and report entire groups missing any routing mode")
    parser.add_argument("--expected-seeds", nargs="+", type=int, default=[],
                        help="Also require these seeds for every observed scenario/load")
    args = parser.parse_args(argv)
    try:
        result = report(args.runs_csv, args.output_dir, input_batch=args.input_batch,
                        allow_incomplete=args.allow_incomplete, expected_seeds=args.expected_seeds)
    except (ValueError, OSError, UnicodeError) as error:
        parser.exit(2, f"h3_report: {error}\n")
    print(f"H3: {result['included_triples']} matched groups; "
          f"{len(result['exclusions'])} excluded; report: {args.output_dir / 'H3.md'}")


if __name__ == "__main__":
    main()
