#!/usr/bin/env python3
"""Bypass vs. baseline.  Compares one sweep's bypass runs with its baseline on the same
flows: H1 (small-flow latency and timeouts), H2 (cost to large flows, hotspots, fan-in)
and H3 (reach across all small flows)."""

import cli

cli.parse(__doc__)

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

import common  # noqa: E402

common.setup_plots()
out = common.Output(common.output_dir("bypass-comparison"), f"Bypass vs. baseline ({common.SWEEP})")
out.text(f"Reading {common.MACHINE}/{common.BATCH}.")
baseline = common.load_runs(common.BASELINE_EXPERIMENT)
bypass = common.load_runs(common.BYPASS_EXPERIMENT)

# Keep only settings and seeds that the baseline and every bypass mode have finished, so
# that all modes in a table cover the same flows.
pairs = {(r.setting, r.seed) for r in baseline}
for mode in {r.bypass for r in bypass}:
    pairs &= {(r.setting, r.seed) for r in bypass if r.bypass == mode}
if not bypass:
    pairs = set()
if not pairs:
    out.text(f"Nothing to compare yet: {len(baseline)} baseline runs and {len(bypass)} bypass runs share no setting "
             f"and seed. Run experiments/{common.BASELINE_EXPERIMENT}.sh and experiments/{common.BYPASS_EXPERIMENT}.sh.")
    out.save()
    raise SystemExit(0)
runs = [r for r in baseline + bypass if (r.setting, r.seed) in pairs]
flows = common.load_flows(runs)
links = common.load_links(runs)
modes = [m for m in common.MODES if m in {r.bypass for r in runs}]
out.text(f"Comparing {len(pairs)} setting and seed pairs: {sorted(pairs)}. Modes: {modes}.")

out.heading("Are the runs healthy and comparable?")
out.table("health", common.runs_table(runs))
identity = ["flow", "source_router", "destination_router", "bytes", "start_ns"]
mismatches = []
for key, group in flows.groupby([*common.SETTINGS, "seed"]):
    reference = group[group["bypass"] == "none"][identity].reset_index(drop=True)
    for mode, other in group.groupby("bypass"):
        if not reference.equals(other[identity].reset_index(drop=True)):
            mismatches.append(f"setting and seed {key}, mode {mode}")
out.text("All modes run the same flows." if not mismatches else f"Different flows at: {'; '.join(mismatches)}.")

out.heading("H1: Does the bypass cut small-flow latency?")
out.heading("Path lengths", 3)
out.table("mean-hops", common.mean_hops_table(runs))
out.table("fct", common.fct_table(flows, ["class", *common.SETTINGS, "bypass"]))
out.figures_from(common.plot_metric_by_mode(flows, "p50", "Median FCT (µs)"), "median-fct")
out.figures_from(common.plot_metric_by_mode(flows, "p99", "p99 FCT (µs)"), "p99-fct")

out.heading("Flow by flow", 3)
keys = [*common.SETTINGS, "seed", "flow"]
reference = flows[flows["bypass"] == "none"][keys + ["fct_us"]].rename(columns={"fct_us": "baseline_us"})
paired = flows[flows["bypass"] != "none"].merge(reference, on=keys)
paired["ratio"] = paired["fct_us"] / paired["baseline_us"]
out.table("flow-by-flow", paired.groupby(["class", *common.SETTINGS, "bypass"]).agg(
    median_ratio=("ratio", "median"),
    faster=("ratio", lambda r: (r < 1).mean()),
    slower=("ratio", lambda r: (r > 1).mean()),
))

out.heading("Why: fewer timeouts", 3)
out.table("timeouts", common.timeout_table(flows, ["class", *common.SETTINGS, "bypass"]))
out.figures_from(common.plot_metric_by_mode(flows, "any_timeout_pct", "Flows with a timeout (%)",
                                            table=common.timeout_table, classes=common.SMALL_CLASSES), "timeouts")
out.figures_from(common.plot_metric_by_mode(flows, "clean_p99", "p99 FCT with no timeout (µs)",
                                            table=common.timeout_table, classes=common.SMALL_CLASSES), "clean-p99")

out.heading("Seed by seed", 3)
out.table("seed-changes", common.seed_change_table(flows))

out.heading("H2: What does it cost, and where is the crossover?")
p99 = common.fct_table(flows, ["class", *common.SETTINGS, "bypass"])["p99"].unstack("bypass")
out.table("p99-ratios", pd.DataFrame({mode: p99[mode] / p99["none"] for mode in p99.columns if mode != "none"}))

out.heading("Large-flow throughput", 3)
goodput = common.goodput_table(flows, [*common.SETTINGS, "bypass"])
out.table("goodput", goodput)
by_mode = goodput[["mean", "p1"]].unstack("bypass")
out.table("goodput-ratios", pd.DataFrame({
    f"{stat}_{mode}": by_mode[(stat, mode)] / by_mode[(stat, "none")]
    for stat in ["mean", "p1"]
    for mode in by_mode[stat].columns if mode != "none"
}))
per_seed = common.goodput_table(flows, [*common.SETTINGS, "seed", "bypass"])[["mean", "p1"]]
reference = per_seed.xs("none", level="bypass")
changes = pd.concat(
    {mode: 100 * (per_seed.xs(mode, level="bypass") / reference - 1) for mode in modes if mode != "none"},
    names=["bypass"],
).add_suffix("_change_pct")
out.table("goodput-changes-by-seed", changes.groupby(["bypass", *common.SETTINGS]).agg(["mean", "min", "max"]))

out.heading("Hotspots", 3)
table = common.link_table(links, [*common.SETTINGS, "bypass"])
out.table("links", table)
for title, rows in common.panels(table.reset_index()):
    figure, axes = plt.subplots(1, 2, figsize=(9, 3.2), sharex=True)
    for axis, column, label in zip(axes, ["max_utilization", "hotspot_ratio"],
                                   ["Busiest link's utilization", "Busiest link / mean link"]):
        for mode in common.MODES:
            group = rows[rows["bypass"] == mode]
            if group.empty:
                continue
            axis.plot(group[common.SETTING], group[column], color=common.MODE_COLORS[mode],
                      marker=common.MODE_MARKERS[mode], label=common.MODE_NAMES[mode])
        axis.set(ylabel=label, ylim=(0, None))
        common.label_setting_axis(axis)
    axes[0].legend()
    if title:
        figure.suptitle(title)
    figure.tight_layout()
    out.figure(figure)

out.heading("Which links got busier", 3)
paired_links = common.link_changes(links)
out.table("hotspot-links", common.hotspot_table(paired_links, [*common.SETTINGS, "bypass"]))
# The most loaded setting of each panel, where a hotspot is likeliest.
for title, rows in common.panels(paired_links):
    top = rows[common.SETTING].max()
    figure, axis = plt.subplots(figsize=(6, 3.4))
    for mode, group in rows[rows[common.SETTING] == top].groupby("bypass"):
        change = np.sort(group["utilization_change"].to_numpy()) * 100
        axis.plot(change, np.linspace(0, 1, len(change)), color=common.MODE_COLORS[mode], label=common.MODE_NAMES[mode])
    axis.axvline(0, color="#b5b4ad", linewidth=1)
    name = common.setting_name(top)
    axis.set(xlabel="Change in link utilization (points)", ylabel="Share of links at or below",
             title=f"{title}, {name}" if title else name)
    axis.legend()
    figure.tight_layout()
    out.figure(figure)

out.heading("Fan-in", 3)
out.table("fan-in", common.fan_in_table(links, [*common.SETTINGS, "bypass"]))

out.heading("H3: How many flows does it reach?")
small = flows[flows["class"] != "large"]
out.text(f"Adjacent share of small flows: {(small['class'] == 'small_adjacent').mean():.1%}.")
out.table("all-small-fct", common.fct_table(small.assign(group="all small"), ["group", *common.SETTINGS, "bypass"]))

out.save()
