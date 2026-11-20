#!/usr/bin/env python3
"""Workload sweep (H2): summarizes 08-workloads and the default-workload 03-load-sweep in
one batch.  Checks routing and per-port drop health, pairs each bypass run with its
baseline on the same flows, and writes per-run and paired tables with raw FCTs, measured
timeouts and retransmissions, goodput, link load and drops.  experiments/workload-queue.py
reruns it as runs finish."""

import hashlib
import json
import os

import cli

if "SWEEP" not in os.environ:
    os.environ["SWEEP"] = "08-workloads"
cli.parse(__doc__)

import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

import common  # noqa: E402


SCENARIOS = ["default", "fraction95", "fraction99", "size50k", "size100k",
             "web10k", "web100k", "web1m", "hadoop10k", "hadoop100k", "hadoop1m"]
SEEDS = [561, 562, 563]


def loads(scenario):
    return [0.1, 0.3, 0.5, 0.7, 0.9] if scenario == "default" else [0.3, 0.5, 0.7]


def expected_keys():
    return {(s, load, seed, mode) for s in SCENARIOS for load in loads(s)
            for seed in SEEDS for mode in common.MODES}


def quantile(series, q):
    return float(common.percentile(series.to_numpy(), q)) if len(series) else float("nan")


def metrics(run, scenario):
    raw = pd.read_csv(run.path / "flows.csv")
    identity = raw[["flow", "source_router", "destination_router", "adjacent", "small",
                    "bytes", "start_ns"]]
    fingerprint = hashlib.sha256(pd.util.hash_pandas_object(identity, index=False).values.tobytes()).hexdigest()
    last = raw.start_ns.max()
    flows = raw[raw.start_ns.between(0.1 * last, 0.9 * last)].copy()
    flows["fct_us"] = ((flows.finish_ns - flows.start_ns) / 1000).fillna(np.inf)
    flows["goodput"] = flows.bytes * 8 / (flows.fct_us * 1000)
    flows["timeout"] = common._timeouts(flows)
    links = pd.read_csv(run.path / "links.csv")
    if "port_kind" not in links:
        raise ValueError(f"{run.path}: per-port drop instrumentation missing")
    fabric = links[links.port_kind == "fabric"].copy()
    fabric["utilization"] = fabric.sent_bytes_during_arrivals / (run.summary["link_gbps"] * 1e9 / 8 * last * 1e-9)
    topology = fabric[["from", "to"]].sort_values(["from", "to"])
    topology_hash = hashlib.sha256(pd.util.hash_pandas_object(topology, index=False).values.tobytes()).hexdigest()
    drops = run.summary["drops"]
    router_drops = int(links.loc[links.port_kind != "host_nic", "queue_drops"].sum())
    host_drops = int(links.loc[links.port_kind == "host_nic", "queue_drops"].sum())
    checks = [router_drops == drops["router_queue"], host_drops == drops["host_queue"],
              int(links.device_drops.sum()) == drops["device"],
              drops.get("unattributed_router_queue", -1) == 0,
              drops["ttl"] == 0, drops["no_route"] == 0, drops["other_ip"] == 0]
    if not all(checks):
        raise ValueError(f"{run.path}: drop attribution or routing health check failed: {drops}")
    row = dict(scenario=scenario, load=run.load, seed=run.seed, bypass=run.bypass,
               flow_fingerprint=fingerprint, topology_fingerprint=topology_hash,
               flows=len(raw), analyzed_flows=len(flows), unfinished=run.summary["flows"] - run.summary["completed"],
               small_byte_share=float(raw.loc[raw.small == 1, "bytes"].sum() / raw.bytes.sum()),
               adjacent_small_byte_share=float(raw.loc[(raw.small == 1) & (raw.adjacent == 1), "bytes"].sum() / raw.bytes.sum()),
               mean_utilization=float(fabric.utilization.mean()), max_utilization=float(fabric.utilization.max()),
               hotspot_ratio=float(fabric.utilization.max() / fabric.utilization.mean()),
               full_queue_share=float((fabric.max_queue_packets >= run.summary["fabric_queue"]).mean()),
               mean_queue=float(fabric.mean_queue_packets_during_arrivals.mean()),
               fabric_drops=int(fabric.queue_drops.sum()),
               downlink_drops=int(links.loc[links.port_kind == "downlink", "queue_drops"].sum()),
               host_drops=host_drops, device_drops=drops["device"])
    delivered = fabric.delivered_small_bytes + fabric.delivered_large_bytes
    fanin = fabric.assign(delivered=delivered).groupby("to").delivered.agg(["sum", "max"])
    row["mean_busiest_neighbor_share"] = float((fanin["max"] / fanin["sum"].replace(0, np.nan)).mean())
    for name, subset in {
        "all": flows, "small": flows[flows.small == 1],
        "small_adjacent": flows[(flows.small == 1) & (flows.adjacent == 1)],
        "small_other": flows[(flows.small == 1) & (flows.adjacent == 0)],
        "large": flows[flows.small == 0],
    }.items():
        row[f"{name}_flows"] = len(subset)
        row[f"{name}_beyond_p99"] = len(subset) // 100
        for q in [0.5, 0.99]:
            row[f"{name}_p{int(q * 100)}_us"] = quantile(subset.fct_us, q)
        row[f"{name}_mean_goodput_gbps"] = float(subset.goodput.mean())
        row[f"{name}_p01_goodput_gbps"] = quantile(subset.goodput, 0.01)
        row[f"{name}_data_retransmitted_share"] = float((subset.data_retransmissions > 0).mean())
        # NaN for runs from before the per-flow TCP counters.
        row[f"{name}_timeout_share"] = (float(subset.timeout.isin(["data", "handshake", "unfinished"]).mean())
                                        if subset.timeout.notna().all() else float("nan"))
    return row


def compare(table):
    keys = ["scenario", "load", "seed"]
    base = table[table.bypass == "none"]
    paired = table[table.bypass != "none"].merge(base, on=keys, suffixes=("", "_baseline"))
    for field in ["flow_fingerprint", "topology_fingerprint"]:
        if not (paired[field] == paired[field + "_baseline"]).all():
            raise ValueError(f"Compared modes do not share the same {field}")
    fields = ["small_p99_us", "small_p50_us", "small_adjacent_p99_us", "small_other_p99_us",
              "large_mean_goodput_gbps", "large_p01_goodput_gbps", "large_p99_us",
              "hotspot_ratio", "max_utilization", "mean_busiest_neighbor_share"]
    for field in fields:
        paired[field + "_change_pct"] = 100 * (paired[field] / paired[field + "_baseline"] - 1)
    return paired


def finished_runs():
    """(scenario, run) for every finished run: the default workload from 03-load-sweep,
    and the other scenarios from 08-workloads."""
    for suffix in ["baseline", "bypass"]:
        for run in common.load_runs(f"03-load-sweep-{suffix}"):
            yield "default", run
        for run in common.load_runs(f"08-workloads-{suffix}"):
            yield run.scenario, run


def report(batch, out):
    out.mkdir(parents=True, exist_ok=True)
    cache = out / "cache.json"
    cached = json.loads(cache.read_text()) if cache.exists() else {}
    rows, seen = [], set()
    for scenario, run in finished_runs():
        key = (scenario, run.load, run.seed, run.bypass)
        if key in seen:
            raise ValueError(f"Duplicate run: {key}")
        if key not in expected_keys():
            raise ValueError(f"Unexpected run: {key}")
        seen.add(key)
        name = str(run.path.relative_to(batch))
        # Final run directories are immutable; include file metadata to detect replacements.
        stamp = [(run.path / file).stat().st_mtime_ns for file in
                 ["flows.csv", "links.csv", "summary.txt", "args.txt"]]
        entry = cached.get(name, {})
        if entry.get("stamp") != stamp:
            entry = {"stamp": stamp, "metrics": metrics(run, scenario)}
            cached[name] = entry
        rows.append(entry["metrics"])
    cache.write_text(json.dumps(cached))
    table = pd.DataFrame(rows)
    table.to_csv(out / "runs.csv", index=False)
    pending = sorted(expected_keys() - seen)
    pd.DataFrame(pending, columns=["scenario", "load", "seed", "bypass"]).to_csv(out / "pending.csv", index=False)
    partial = list(batch.glob("*-*/**/*.partial"))
    failed = list(batch.glob("*-*/**/*.failed"))
    lines = ["# Workload sweep (H2)", "", f"Batch: `{batch.name}`.", "",
             f"Finished: **{len(seen)}/{len(expected_keys())}**. Pending: **{len(pending)}**. Failed directories: **{len(failed)}**.", "",
             "Results below use the middle 80% of flow arrivals. FCT is raw, without inferred timeout filtering. "
             "Data retransmissions exclude handshakes. Link utilization covers the arrival window; queue/drop "
             "counts cover the full simulation. Every completed run passed routing and per-port drop reconciliation.", "",
             "Seed ranges are descriptive, without confidence intervals. A crossover candidate requires worse "
             "small-flow p99 in every matched seed; large-flow goodput losses are reported separately. "
             "Three matching seeds are required before labeling a candidate. A candidate is not statistical confirmation.", ""]
    if rows:
        paired = compare(table)
        paired.to_csv(out / "comparisons.csv", index=False)
        lines += ["## Paired routing comparisons", "",
                  "Percent changes are means across matched seeds. Negative FCT changes and positive goodput changes are improvements.", "",
                  "| Scenario | Load | Bypass | Seeds | Small p99 Δ | Large mean goodput Δ | Seeds with worse small p99 / lower large goodput |",
                  "| --- | ---: | --- | ---: | ---: | ---: | --- |"]
        candidates = []
        for (scenario, load, mode), group in paired.groupby(["scenario", "load", "bypass"]):
            tail = group.small_p99_us_change_pct
            goodput = group.large_mean_goodput_gbps_change_pct
            worse = int((tail > 0).sum())
            slower = int((goodput < 0).sum())
            lines.append(f"| {scenario} | {load:g} | {mode} | {len(group)} | {tail.mean():+.1f}% | {goodput.mean():+.1f}% | {worse} / {slower} |")
            if len(group) == 3 and worse == 3:
                candidates.append(f"{scenario}, {mode}, load {load:g}")
        lines += ["", "## Crossover status", ""]
        lines.append("Small-flow p99 crossover candidates: " + "; ".join(candidates) + "." if candidates else
                     "No completed three-seed comparison has worse small-flow p99 in every seed so far.")
        if pending:
            lines.append("The matrix is incomplete; pending conditions cannot be interpreted as evidence of no crossover.")
        else:
            lines.append("This bounds the tested uniform-traffic parameter grid only; it does not rule out crossovers under other conditions.")
        lines += ["", "## Bypass traffic volume", "",
                  "| Scenario | Small share of bytes | Adjacent small share of bytes | Small adjacent tail samples (minimum beyond p99) |",
                  "| --- | ---: | ---: | ---: |"]
        for scenario, group in table[table.bypass == "none"].groupby("scenario"):
            lines.append(f"| {scenario} | {100 * group.small_byte_share.mean():.2f}% | {100 * group.adjacent_small_byte_share.mean():.2f}% | {group.small_adjacent_beyond_p99.min()} |")
        lines += ["", "The CSVs contain per-seed p50/p99, measured timeout and data retransmission shares, large-flow goodput, "
                  "fan-in, busiest-link utilization, queue occupancy and drops separated by port kind. "
                  "Fewer than 100 observations beyond p99 requires caution; unfinished flows count as infinite FCT."]
    if partial:
        lines += ["", "Currently incomplete run directories: " + ", ".join(f"`{p.relative_to(batch)}`" for p in partial)]
    if failed:
        lines += ["", "Failed run directories: " + ", ".join(f"`{p.relative_to(batch)}`" for p in failed)]
    destination = out / "README.md"
    destination.write_text("\n".join(lines) + "\n")
    print(f"Workloads: {len(seen)}/{len(expected_keys())} runs; report: {destination}")


common.RESULTS = common.RESULTS.resolve()
report(common.RESULTS, common.output_dir("summary"))
