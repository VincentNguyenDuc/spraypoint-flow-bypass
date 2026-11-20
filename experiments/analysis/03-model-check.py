#!/usr/bin/env python3
"""Model check: the bypass models against the routing tables and ns-3.  Compares the
flow-level check (05-bypass-model) with the report's models, then the permutation sweep
(06-permutation by default) with the flow-level check, pair by pair."""

import os

import cli

args = cli.parse(__doc__)
if not args.sweep:
    os.environ.setdefault("SWEEP", "06-permutation")

import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

import common  # noqa: E402

common.setup_plots()
out = common.Output(common.output_dir("model-check"), f"Model check ({common.SWEEP})")
out.text(f"Reading {common.MACHINE}/{common.BATCH}.")
oversubscription, pair_loads = common.load_model_checks()
scales = sorted(set(zip(oversubscription["nodes"], oversubscription["degree"]))) if len(oversubscription) else []
runs_checked = oversubscription[["nodes", "degree", "seed"]].drop_duplicates().shape[0] if scales else 0
out.text(f"Flow-level check: {runs_checked} runs at scales {scales}.")

out.heading("1. Flow-level check")
out.text(
    "Each row averages over permutations. `ecmp_kept` and `lp_kept` are the throughput every pair keeps "
    "relative to no bypass: oversubscription without the bypass over oversubscription with it, per permutation. "
    "The ECMP model is O(1 - f) + f d / c, with O each permutation's measured oversubscription without the bypass "
    "and c its weakest bypassed pair's shortest paths (report Section 5, Step 7). The ideal model is "
    "max(1/sum(mu), f d / c) (Step 6). `lp_worst` is the worst permutation's LP oversubscription; above the ideal "
    "model, single-path pairs share a link."
)
if scales:
    checks = oversubscription.copy()
    checks["lp_r"] = (checks["lp_lower"] + checks["lp_upper"]) / 2
    keys = ["nodes", "degree", "seed", "matching"]
    baseline = checks[checks["mode"] == "none"].set_index(keys)[["ecmp_r", "lp_r", "model_ideal"]].add_suffix("_none")
    # Older batches also checked the adjacent bypass; only the shortest-path bypass is analyzed.
    checks = checks[checks["mode"] == "shortest"].join(baseline, on=keys)
    checks["ecmp_kept"] = checks["ecmp_r_none"] / checks["ecmp_r"]
    checks["lp_kept"] = checks["lp_r_none"] / checks["lp_r"]
    checks["model_ecmp_kept"] = checks["ecmp_r_none"] / (
        checks["ecmp_r_none"] * (1 - checks["share"]) + checks["share"] * checks["degree"] / checks["min_c"]
    )
    checks["model_ideal_kept"] = checks["model_ideal_none"] / checks["model_ideal"]
    out.table("flow-level", checks.groupby(["nodes", "degree", "mode", "share"]).agg(
        permutations=("matching", "size"),
        ecmp_r=("ecmp_r", "mean"),
        ecmp_kept=("ecmp_kept", "mean"),
        model_ecmp_kept=("model_ecmp_kept", "mean"),
        lp_r=("lp_r", "mean"),
        lp_worst=("lp_upper", "max"),
        lp_kept=("lp_kept", "mean"),
        model_ideal_kept=("model_ideal_kept", "mean"),
    ), float_format="{:,.3f}")

    figure, axes = plt.subplots(1, len(scales), figsize=(5 * len(scales), 3.6), sharey=True)
    axes = np.atleast_1d(axes)
    for axis, (nodes, degree) in zip(axes, scales):
        rows = checks[(checks["nodes"] == nodes) & (checks["degree"] == degree)]
        mean = rows.groupby(["mode", "share"])[["ecmp_kept", "lp_kept", "model_ecmp_kept", "model_ideal_kept"]].mean()
        data = mean.xs("shortest", level="mode")
        color = common.MODE_COLORS["shortest"]
        axis.plot(data.index * 100, data["ecmp_kept"] * 100, color=color, marker=common.MODE_MARKERS["shortest"],
                  label="ECMP")
        axis.plot(data.index * 100, data["lp_kept"] * 100, color=color, linestyle="--", label="Best split")
        axis.plot(data.index * 100, data["model_ecmp_kept"] * 100, color="#52514e", linewidth=1, label="ECMP model")
        axis.plot(data.index * 100, data["model_ideal_kept"] * 100, color="#52514e", linewidth=1, linestyle=":",
                  label="Ideal model")
        axis.set(title=f"n = {nodes}, d = {degree}", xlabel="Small flows' share of bytes, f (%)", ylim=(0, 105))
    axes[0].set_ylabel("Throughput kept vs no bypass (%)")
    axes[-1].legend(fontsize=8)
    figure.tight_layout()
    out.figure(figure, "flow-level")

out.heading("2. ns-3 under permutation traffic")
out.text(
    "Every router's hosts send only to their partner router, the same pairs as matching 0 of the flow-level check "
    "for that seed. Each pair's throughput is the mean goodput of its large flows, and `kept` is that over the same "
    "pair's goodput with no bypass, on the same flows. Pairs are grouped as the models group them: adjacent pairs "
    "and 2-hop pairs with one shortest path have c = 1 and should lose the most once the bypass loads their single path."
)
baseline = common.load_runs(common.BASELINE_EXPERIMENT)
bypass = common.load_runs(common.BYPASS_EXPERIMENT)
router_patterns = {"router-permutation"}
if common.SWEEP == "06-permutation":
    # Earlier runs of this sweep recorded the router pattern under its original name.
    router_patterns.add("permutation")
permutation = [r for r in baseline + bypass if r.options.get("pattern") in router_patterns]
complete = {(r.setting, r.seed) for r in permutation if r.bypass == "none"}
for mode in {r.bypass for r in permutation if r.bypass != "none"}:
    complete &= {(r.setting, r.seed) for r in permutation if r.bypass == mode}
if not any(r.bypass != "none" for r in permutation):
    complete = set()
runs = [r for r in permutation if (r.setting, r.seed) in complete]
if not runs or pair_loads.empty:
    out.text(f"Nothing to compare yet: {len(permutation)} finished permutation runs, "
             f"{'with' if not pair_loads.empty else 'without'} a flow-level check in this batch.")
    out.save()
    raise SystemExit(0)
modes = [m for m in common.MODES if m in {r.bypass for r in runs}]
flows = common.load_flows(runs)
out.text(f"Comparing {len(complete)} setting and seed pairs, modes {modes}.")

KINDS = ["neighbors (c = 1)", "2 hops, c = 1", "2 hops, c = 2", "2 hops, c ≥ 3", "3+ hops"]
KIND_COLORS = ["#e34a33", "#fc8d59", "#91bfdb", "#4575b4", "#52514e"]


def pair_kind(distance, c):
    if distance == 1:
        return "neighbors (c = 1)"
    if distance == 2:
        return "2 hops, c = 1" if c == 1 else ("2 hops, c = 2" if c == 2 else "2 hops, c ≥ 3")
    return "3+ hops"


nodes, degree = int(runs[0].options["nodes"]), int(runs[0].options["degree"])
keys = [*common.SETTINGS, "seed", "source_router", "destination_router"]
large = flows[flows["class"] == "large"]
per_pair = large.groupby(keys + ["bypass"])["goodput_gbps"].mean().unstack("bypass")
for mode in modes:
    if mode != "none":
        per_pair[f"kept_{mode}"] = per_pair[mode] / per_pair["none"]
per_pair = per_pair.reset_index()

# Each pair's distance and c, from the flow-level check's matching 0 for the same seed.
shape = pair_loads[(pair_loads["nodes"] == nodes) & (pair_loads["degree"] == degree)
                   & (pair_loads["matching"] == 0) & (pair_loads["mode"] == "none")]
shape = shape.rename(columns={"source": "source_router", "destination": "destination_router"})[
    ["seed", "source_router", "destination_router", "distance", "c"]]
per_pair = per_pair.merge(shape, on=["seed", "source_router", "destination_router"], how="left")
missing = per_pair["c"].isna().sum()
out.text(f"{len(per_pair)} pair-settings; {missing} not in the flow-level check's matching 0"
         + (" (check the seeds and network size)." if missing else "."))
per_pair = per_pair.dropna(subset=["c"])
per_pair["kind"] = pd.Categorical([pair_kind(d, c) for d, c in zip(per_pair["distance"], per_pair["c"])],
                                  categories=KINDS, ordered=True)
kept = [f"kept_{m}" for m in modes if m != "none"]
summary = per_pair.groupby([*common.SETTINGS, "kind"], observed=True)[kept].agg(["mean", "min"])
summary.columns = [f"{column}_{stat}" for column, stat in summary.columns]
out.table("pairs-by-kind",
          summary.join(per_pair.groupby([*common.SETTINGS, "kind"], observed=True).size().rename("pairs")),
          float_format="{:,.3f}")

for title, rows in common.panels(per_pair):
    figure, axes = plt.subplots(1, len(kept), figsize=(5 * len(kept), 3.4), sharey=True)
    axes = np.atleast_1d(axes)
    for axis, column in zip(axes, kept):
        mean = rows.groupby(["kind", common.SETTING], observed=True)[column].mean()
        for kind in (k for k in KINDS if k in mean.index.get_level_values("kind")):
            data = mean.xs(kind, level="kind")
            axis.plot(data.index, data * 100, marker="o", color=KIND_COLORS[KINDS.index(kind)], label=kind)
        axis.axhline(100, color="#b5b4ad", linewidth=1)
        axis.set(title=common.MODE_NAMES[column.removeprefix("kept_")])
        common.label_setting_axis(axis)
    axes[0].set_ylabel("Large-flow throughput kept (%)")
    axes[-1].legend(fontsize=8)
    if title:
        figure.suptitle(title)
    figure.tight_layout()
    out.figure(figure)

out.heading("Does ns-3 slow down the pairs the flow-level check says are loaded?", 3)
out.text(
    "For each pair, the flow-level check gives the busiest link its large flows cross under ECMP, at the nearest "
    "share it computed (matching 0, same seed). If the ECMP model describes ns-3, pairs whose links it says are "
    "busier should get less throughput. `spearman` is the rank correlation between the two at the highest load; "
    "negative means they agree."
)
top = per_pair[common.SETTING].max()
predicted = pair_loads[(pair_loads["nodes"] == nodes) & (pair_loads["degree"] == degree) & (pair_loads["matching"] == 0)]
predicted = predicted.rename(columns={"source": "source_router", "destination": "destination_router"})
shares = sorted(predicted["share"].unique())
rows = []
at_top = per_pair[per_pair[common.SETTING] == top].assign(share=lambda f: f.get("share", 0))
for (share, mode), group in at_top.melt(
    id_vars=["share", "seed", "source_router", "destination_router", "kind"], value_vars=modes,
    var_name="mode", value_name="goodput",
).groupby(["share", "mode"]):
    nearest = 0 if mode == "none" else min((s for s in shares if s > 0), key=lambda s: abs(s - share))
    model = predicted[(predicted["share"] == nearest) & (predicted["mode"] == mode)][
        ["seed", "source_router", "destination_router", "large_bottleneck"]]
    joined = group.merge(model, on=["seed", "source_router", "destination_router"])
    rows.append({"share": share, "mode": mode, "model_share": nearest, "pairs": len(joined),
                 # Spearman: the correlation of the ranks, without needing scipy.
                 "spearman": joined["goodput"].rank().corr(joined["large_bottleneck"].rank())})
out.table("rank-agreement", pd.DataFrame(rows).set_index(["share", "mode"]), float_format="{:,.3f}")

out.save()
