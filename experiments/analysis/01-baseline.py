#!/usr/bin/env python3
"""Baseline: default Spraypoint.  Summarizes one sweep's baseline runs: health, traffic,
completion times and their tail, timeouts, large-flow throughput, paths, links and fan-in."""

import cli

cli.parse(__doc__)

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402

import common  # noqa: E402

common.setup_plots()
out = common.Output(common.output_dir("baseline"), f"Baseline: default Spraypoint ({common.SWEEP})")

runs = common.load_runs(common.BASELINE_EXPERIMENT)
out.text(f"Reading {common.MACHINE}/{common.BATCH}.")
if not runs:
    out.text(f"No finished runs of {common.BASELINE_EXPERIMENT}. Run experiments/{common.BASELINE_EXPERIMENT}.sh.")
    out.save()
    raise SystemExit(0)
flows = common.load_flows(runs)
links = common.load_links(runs)
settings = ", ".join(common.SETTING_LABELS[s].lower() for s in common.SETTINGS)
out.text(f"{len(runs)} finished runs, {settings} {sorted({r.setting for r in runs})}, "
         f"seeds {sorted({r.seed for r in runs})}.")

out.heading("Are the runs healthy?")
health = common.runs_table(runs)
out.table("health", health)
problems = health[(health["unfinished"] > 0) | (health["ttl_drops"] > 0) | (health["no_route_drops"] > 0)]
out.text("All runs healthy." if problems.empty else f"Check these runs: {', '.join(map(str, problems.index))}.")

out.heading("What traffic is it?")
out.table("traffic-mix", common.traffic_mix(flows, common.SETTINGS).unstack("class"))

out.heading("Flow completion times")
out.table("fct", common.fct_table(flows, ["class", *common.SETTINGS]))

out.heading("The tail of the distribution", 3)
colors = common.setting_colors(flows[common.SETTING])
for title, rows in common.panels(flows):
    figure, axes = plt.subplots(1, 3, figsize=(11, 3.4), sharey=True)
    for axis, flow_class in zip(axes, common.CLASSES):
        for value, group in rows[rows["class"] == flow_class].groupby(common.SETTING):
            x, y = common.tail_curve(group["fct_us"].to_numpy())
            axis.step(x, y, color=colors[value], label=common.setting_name(value), where="post")
        axis.set(title=common.CLASS_NAMES[flow_class], xscale="log", yscale="log", xlabel="FCT (µs)")
    axes[0].set_ylabel("Share of flows slower")
    axes[0].legend()
    if title:
        figure.suptitle(title)
    figure.tight_layout()
    out.figure(figure)

out.heading("Where the tail comes from", 3)
if caveat := common.timeout_caveat(flows):
    out.text(caveat)
out.table("timeouts", common.timeout_table(flows, ["class", *common.SETTINGS]))
timeouts = common.timeout_table(flows, ["class", *common.SETTINGS, "seed"]).reset_index()
for title, rows in common.panels(timeouts):
    figure, axes = plt.subplots(1, 2, figsize=(8, 3.4), sharey=True)
    for axis, flow_class in zip(axes, common.SMALL_CLASSES):
        data = rows[rows["class"] == flow_class].groupby(common.SETTING)[["data_timeout_pct", "handshake_timeout_pct"]].mean()
        axis.plot(data.index, data["data_timeout_pct"], color="#2a78d6", marker="o", label="Data (1 ms)")
        axis.plot(data.index, data["handshake_timeout_pct"], color="#104281", marker="s", linestyle="--",
                  label="Handshake (10 ms)")
        axis.axhline(1, color="#b5b4ad", linewidth=1)
        axis.set(title=common.CLASS_NAMES[flow_class])
        common.label_setting_axis(axis)
    axes[0].set_ylim(bottom=0)
    axes[0].set_ylabel("Flows waiting for a timeout (%)")
    axes[0].legend(title="Timeout")
    if title:
        figure.suptitle(title)
    figure.tight_layout()
    out.figure(figure)

out.heading("Large-flow throughput")
out.table("goodput", common.goodput_table(flows, common.SETTINGS))

out.heading("Paths")
paths = common.path_table(runs).groupby(["class", "hops"])["fraction"].mean().reset_index()
figure, axes = plt.subplots(1, 3, figsize=(11, 3), sharey=True)
for axis, flow_class in zip(axes, common.CLASSES):
    data = paths[(paths["class"] == flow_class) & (paths["hops"] <= 6)]
    axis.bar(data["hops"], data["fraction"], color=common.MODE_COLORS["none"], width=0.7)
    axis.set(title=common.CLASS_NAMES[flow_class], xlabel="Fabric hops")
    axis.grid(axis="x", visible=False)
axes[0].set_ylabel("Share of packets")
figure.tight_layout()
out.figure(figure)

out.heading("Links")
out.table("links", common.link_table(links, common.SETTINGS))
for title, rows in common.panels(links):
    figure, axis = plt.subplots(figsize=(6, 3.4))
    for value, group in rows.groupby(common.SETTING):
        utilization = np.sort(group["utilization"].to_numpy())
        axis.plot(utilization, np.linspace(0, 1, len(utilization)), color=colors[value], label=common.setting_name(value))
    axis.set(xlabel="Link utilization", ylabel="Share of links at or below", xlim=(0, None), title=title)
    axis.legend()
    figure.tight_layout()
    out.figure(figure)

out.heading("Fan-in")
out.table("fan-in", common.fan_in_table(links, common.SETTINGS))

out.save()
