"""Writes the report's figures and tables from the result batches.

    experiments/analysis/.venv/bin/python experiments/analysis/report_figures.py

Figures go to report/figures/ as PDF, and tables to report/tables/ as LaTeX
tabular fragments that the report \\inputs.  Both are committed, since the results are
not.  The batches are pinned below, so the report always shows the same runs; a figure
whose batch is not pinned or missing is skipped with a warning, so the rest still regenerate.
"""

import sys
import warnings
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402

import common  # noqa: E402

warnings.filterwarnings("ignore", category=FutureWarning)

REMOTE = common.RESULTS_ROOT / "remote"
# The batch each sweep's figures come from, such as
#     "03-load-sweep": REMOTE / "2026-11-20_120000_abc1234",
# so the report always shows the same runs.  Pin each sweep once its rerun finishes.
BATCHES = {}
REPORT = common.ROOT / "report"
FIGURES = REPORT / "figures"
TABLES = REPORT / "tables"

# One column of the report is 6.5 inches wide.
WIDTH = 6.5
SMALL = ["small_adjacent", "small_other"]
SHORT_CLASS = {"small_adjacent": "Small, neighboring routers", "small_other": "Small, other routers", "large": "Large"}
TABLE_CLASS = {"small_adjacent": "Neighbors", "small_other": "Other pairs"}
MODES = common.MODES
TABLE_MODE = {"none": "None", "shortest": "Shortest-path"}
KINDS = ["Neighboring", "2 hops, c = 1", "2 hops, c = 2", "2 hops, c ≥ 3", "3+ hops"]


def available(sweep):
    if sweep not in BATCHES:
        print(f"warning: no batch pinned for {sweep}; skipping its figures and tables", file=sys.stderr)
        return False
    path = BATCHES[sweep] / (sweep if sweep == "05-bypass-model" else f"{sweep}-baseline")
    if not path.is_dir():
        print(f"warning: no {sweep} results at {path}; skipping its figures and tables", file=sys.stderr)
        return False
    return True


def sweep_runs(sweep):
    common.select(sweep, BATCHES[sweep])
    return common.load_runs(common.BASELINE_EXPERIMENT) + common.load_runs(common.BYPASS_EXPERIMENT)


def save(figure, name):
    figure.savefig(FIGURES / f"{name}.pdf", bbox_inches="tight")
    plt.close(figure)
    print(f"wrote figures/{name}.pdf")


def write_table(name, header, rows, align, groups=None):
    """A booktabs tabular.  rows are lists of cell strings; groups is a list of row indices
    before which a \\midrule goes, to separate blocks."""
    lines = [f"\\begin{{tabular}}{{@{{}}{align}@{{}}}}", "\\toprule", " & ".join(header) + " \\\\", "\\midrule"]
    for i, row in enumerate(rows):
        if groups and i in groups and i > 0:
            lines.append("\\midrule")
        lines.append(" & ".join(row) + " \\\\")
    lines += ["\\bottomrule", "\\end{tabular}", ""]
    (TABLES / f"{name}.tex").write_text("\n".join(lines))
    print(f"wrote tables/{name}.tex")


def pct(value, digits=1):
    return f"{value:.{digits}f}\\%"


def signed(value, digits=1):
    """A signed percentage with a typeset minus sign."""
    return f"{value:+.{digits}f}\\%".replace("-", "$-$")


def style():
    common.setup_plots()
    plt.rcParams.update({"figure.facecolor": "white", "axes.facecolor": "white", "savefig.facecolor": "white",
                         "font.size": 9, "axes.titlesize": 10, "axes.labelsize": 9, "legend.fontsize": 8,
                         "xtick.labelsize": 8, "ytick.labelsize": 8, "lines.linewidth": 1.6, "lines.markersize": 5})


def mode_line(axis, x, y, mode, **extra):
    axis.plot(x, y, color=common.MODE_COLORS[mode], marker=common.MODE_MARKERS[mode],
              label=common.MODE_NAMES[mode], **extra)


# ---------------------------------------------------------------------------------------
# Uniform traffic: 03-load-sweep and 04-oversubscription


def load_sweep():
    runs = sweep_runs("03-load-sweep")
    flows = common.load_flows(runs)

    # Path lengths of small flows, measured in ns-3 at load 0.5.
    paths = common.path_table([r for r in runs if r.load == 0.5])
    paths = paths[paths["class"].isin(SMALL) & paths["hops"].between(1, 5)]
    paths = paths.groupby(["class", "bypass", "hops"])["fraction"].mean()
    figure, axes = plt.subplots(1, 2, figsize=(WIDTH, 2.3), sharey=True)
    for axis, flow_class in zip(axes, SMALL):
        for i, mode in enumerate(MODES):
            data = paths.xs((flow_class, mode), level=["class", "bypass"]).reindex(range(1, 6), fill_value=0)
            axis.bar(data.index + (i - 0.5) * 0.36, data * 100, width=0.36, color=common.MODE_COLORS[mode],
                     label=common.MODE_NAMES[mode])
        axis.set(title=SHORT_CLASS[flow_class], xlabel="Fabric hops", xticks=range(1, 6))
        axis.grid(axis="x", visible=False)
    axes[0].set_ylabel("Share of packets (%)")
    axes[1].legend()
    save(figure, "path-lengths")

    # Median and timeouts against load.
    medians = common.fct_table(flows, ["class", "load", "bypass"])["p50"]
    timeouts = common.timeout_table(flows, ["class", "load", "bypass"])
    figure, axes = plt.subplots(2, 2, figsize=(WIDTH, 4.2), sharex=True)
    for column, flow_class in enumerate(SMALL):
        for mode in MODES:
            m = medians.xs((flow_class, mode), level=["class", "bypass"])
            t = timeouts["any_timeout_pct"].xs((flow_class, mode), level=["class", "bypass"])
            mode_line(axes[0, column], m.index, m, mode)
            mode_line(axes[1, column], t.index, t, mode)
        axes[0, column].set(title=SHORT_CLASS[flow_class], ylim=(0, None))
        axes[1, column].set(xlabel="Offered load", ylim=(0, None))
    axes[0, 0].set_ylabel("Median FCT (µs)")
    axes[1, 0].set_ylabel("Flows with a timeout (%)")
    axes[1, 0].legend(loc="upper left")
    save(figure, "uniform-latency")

    # Table: everything about small flows at load 0.5.
    hops = common.mean_hops_table([r for r in runs if r.load == 0.5])[0.5]
    t05 = timeouts.xs(0.5, level="load")
    p50 = medians.xs(0.5, level="load")
    p99 = common.fct_table(flows[flows["load"] == 0.5], ["class", "bypass"])["p99"]
    rows, groups = [], []
    for flow_class in SMALL:
        groups.append(len(rows))
        for mode in MODES:
            rows.append([TABLE_CLASS[flow_class] if mode == "none" else "", TABLE_MODE[mode],
                         f"{hops[(flow_class, mode)]:.2f}", f"{p50[(flow_class, mode)]:.1f}",
                         f"{t05.loc[(flow_class, mode), 'clean_p99']:.1f}",
                         pct(t05.loc[(flow_class, mode), "any_timeout_pct"], 2),
                         f"{p99[(flow_class, mode)]:,.0f}".replace(",", "{,}")])
    write_table("uniform-h1", ["Small flows", "Bypass", "Hops", "Median ($\\mu$s)", "Clean p99 ($\\mu$s)", "Timeouts", "Raw p99 ($\\mu$s)"],
                rows, "llrrrrr", groups)

    # Large flows and links at load 0.5, for the hosts-per-router comparison.
    return flows[flows["load"] == 0.5], [r for r in runs if r.load == 0.5]


def oversubscription(flows_four, runs_four):
    runs = sweep_runs("04-oversubscription")
    flows = pd.concat([flows_four, common.load_flows(runs)], ignore_index=True)
    common.select("04-oversubscription", BATCHES["04-oversubscription"])
    links = pd.concat([common.load_links(runs_four), common.load_links(runs)], ignore_index=True)
    timeouts = common.timeout_table(flows, ["class", "hosts", "bypass"])["any_timeout_pct"]

    figure, axes = plt.subplots(1, 2, figsize=(WIDTH, 2.4), sharey=True)
    for axis, flow_class in zip(axes, SMALL):
        for mode in MODES:
            data = timeouts.xs((flow_class, mode), level=["class", "bypass"])
            mode_line(axis, data.index, data, mode)
        axis.set(title=SHORT_CLASS[flow_class], xlabel="Hosts per router (load 0.5)", xticks=[4, 6, 8])
    axes[0].set_ylim(bottom=0)
    axes[0].set_ylabel("Flows with a timeout (%)")
    axes[0].legend()
    save(figure, "uniform-hosts")

    goodput = common.goodput_table(flows, ["hosts", "bypass"])["mean"].unstack("bypass")
    utilization = common.link_table(links, ["hosts", "bypass"])["mean_utilization"].unstack("bypass")
    drops = pd.Series({(r.hosts, r.bypass): r.summary["drops"]["router_queue"] for r in runs_four + runs}).groupby(level=[0, 1]).mean().unstack()
    rows = []
    for hosts in [4, 6, 8]:
        rows.append([str(hosts), f"{utilization.loc[hosts, 'none']:.2f}", f"{drops.loc[hosts, 'none'] / 1000:,.0f}k".replace(",", "{,}"),
                     pct(timeouts[("small_adjacent", hosts, "none")], 2) + " $\\to$ " + pct(timeouts[("small_adjacent", hosts, "shortest")], 2),
                     pct(timeouts[("small_other", hosts, "none")], 2) + " $\\to$ " + pct(timeouts[("small_other", hosts, "shortest")], 2),
                     f"{goodput.loc[hosts, 'none']:.1f}",
                     signed(100 * (goodput.loc[hosts, 'shortest'] / goodput.loc[hosts, 'none'] - 1))])
    write_table("uniform-hosts", ["Hosts", "Util.", "Drops", "Timeouts, neighbors", "Timeouts, others",
                                  "Large (Gbps)", "Change"], rows, "rrrccrr")


# ---------------------------------------------------------------------------------------
# Permutation traffic and the models: 05-bypass-model and 06-permutation


def model_checks():
    common.select("06-permutation", BATCHES["05-bypass-model"])
    oversub, pair_loads = common.load_model_checks()
    oversub = oversub.copy()
    oversub["lp_r"] = (oversub["lp_lower"] + oversub["lp_upper"]) / 2
    keys = ["nodes", "degree", "seed", "matching"]
    base = oversub[oversub["mode"] == "none"].set_index(keys)[["ecmp_r", "lp_r", "model_ideal"]].add_suffix("_none")
    checks = oversub[oversub["mode"] != "none"].join(base, on=keys)
    checks["ecmp_kept"] = checks["ecmp_r_none"] / checks["ecmp_r"]
    checks["lp_kept"] = checks["lp_r_none"] / checks["lp_r"]
    checks["model_ecmp_kept"] = checks["ecmp_r_none"] / (
        checks["ecmp_r_none"] * (1 - checks["share"]) + checks["share"] * checks["degree"] / checks["min_c"])
    checks["model_ideal_kept"] = checks["model_ideal_none"] / checks["model_ideal"]

    scales = [(1000, 64), (256, 16)]
    figure, axes = plt.subplots(1, 2, figsize=(WIDTH, 2.7), sharey=True)
    for axis, (nodes, degree) in zip(axes, scales):
        mean = checks[(checks["nodes"] == nodes)].groupby(["mode", "share"])[
            ["ecmp_kept", "lp_kept", "model_ecmp_kept", "model_ideal_kept"]].mean()
        data = mean.xs("shortest", level="mode")
        axis.plot(data.index * 100, data["ecmp_kept"] * 100, color=common.MODE_COLORS["shortest"],
                  marker=common.MODE_MARKERS["shortest"], label="Checker, ECMP")
        axis.plot(data.index * 100, data["lp_kept"] * 100, color=common.MODE_COLORS["shortest"], linestyle="--",
                  label="Checker, best split")
        axis.plot(data.index * 100, data["model_ecmp_kept"] * 100, color="#52514e", linewidth=1, label="ECMP model")
        axis.plot(data.index * 100, data["model_ideal_kept"] * 100, color="#52514e", linewidth=1, linestyle=":",
                  label="Ideal model")
        axis.set(title=f"n = {nodes}, d = {degree}", xlabel="Small flows' share of bytes, f (%)", ylim=(0, 105))
    axes[0].set_ylabel("Throughput kept (%)")
    axes[1].legend(fontsize=7)
    save(figure, "model-throughput")

    rows, groups = [], []
    for nodes, degree in scales:
        groups.append(len(rows))
        table = checks[checks["nodes"] == nodes].groupby(["mode", "share"]).agg(
            ecmp=("ecmp_kept", "mean"), model_ecmp=("model_ecmp_kept", "mean"),
            lp=("lp_kept", "mean"), model_ideal=("model_ideal_kept", "mean"), lp_worst=("lp_upper", "max"))
        for share in [0.01, 0.05, 0.1, 0.2, 0.3]:
            s = table.loc[("shortest", share)]
            rows.append([f"$n = {nodes}$" if share == 0.01 else "", pct(100 * share, 0),
                         pct(100 * s.model_ecmp, 0), pct(100 * s.ecmp, 0),
                         pct(100 * s.model_ideal, 0), pct(100 * s.lp, 0)])
    write_table("model-throughput", ["Network", "$f$", "ECMP model", "ECMP, checker",
                                     "Ideal model", "Best split, checker"], rows, "lrrrrr", groups)

    # Shortest distances at the paper's scale.
    common.select("06-permutation", BATCHES["05-bypass-model"])
    distances = pd.concat([pd.read_csv(p / "distances.csv") for p in sorted((BATCHES["05-bypass-model"] / "05-bypass-model").glob("n-1000_*"))])
    distances = distances.groupby("distance")[["fraction", "model_fraction"]].mean()
    mean = (distances.index * distances["fraction"]).sum()
    model_mean = (distances.index * distances["model_fraction"]).sum()
    rows = [[str(k), pct(100 * v.model_fraction), pct(100 * v.fraction)] for k, v in distances.iterrows()]
    rows.append(["Mean", f"{model_mean:.2f}", f"{mean:.2f}"])
    write_table("bypass-distances", ["Hops", "Model", "Measured"], rows, "lrr", [len(rows) - 1])
    return pair_loads, checks


def pair_kind(distance, c):
    if distance == 1:
        return KINDS[0]
    if distance == 2:
        return KINDS[1] if c == 1 else (KINDS[2] if c == 2 else KINDS[3])
    return KINDS[4]


def permutation(pair_loads, checks):
    runs = sweep_runs("06-permutation")
    flows = common.load_flows(runs)
    timeouts = common.timeout_table(flows, ["class", "share", "load", "bypass"])["any_timeout_pct"]

    shares = sorted(flows["share"].unique())
    figure, axes = plt.subplots(1, len(shares), figsize=(WIDTH, 2.4), sharey=True)
    for axis, share in zip(axes, shares):
        for mode in MODES:
            data = timeouts.xs(("small_adjacent", share, mode), level=["class", "share", "bypass"])
            mode_line(axis, data.index, data, mode)
        axis.set(title=f"f = {round(100 * share, 1):g}%", xlabel="Offered load")
    axes[0].set_ylim(bottom=0)
    axes[0].set_ylabel("Timeouts, small flows\nbetween neighbors (%)")
    axes[0].legend()
    save(figure, "permutation-timeouts")

    rows, groups = [], []
    for share in shares:
        groups.append(len(rows))
        for load in sorted(flows["load"].unique()):
            rows.append([pct(100 * share) if load == min(flows["load"]) else "", f"{load:g}"] + [
                pct(timeouts[("small_adjacent", share, load, mode)], 2) for mode in MODES] + [
                pct(timeouts[("small_other", share, load, m)], 2) for m in ["none", "shortest"]])
    write_table("permutation-timeouts", ["$f$", "Load", "Neighbors: none", "Neighbors: shortest", "Others: none",
                                         "Others: shortest"], rows, "lrrrrr", groups)

    # Slowest 1% of large flows, relative to no bypass, per seed then averaged.
    p1 = common.goodput_table(flows, ["share", "load", "seed", "bypass"])["p1"].unstack("bypass")
    p1_change = (100 * (p1["shortest"] / p1["none"] - 1)).groupby(["share", "load"]).mean()
    mean = common.goodput_table(flows, ["share", "load", "seed", "bypass"])["mean"].unstack("bypass")
    mean_change = (100 * (mean["shortest"] / mean["none"] - 1)).groupby(["share", "load"]).mean()
    rows = [[pct(100 * s) if l == 0.3 else "", f"{l:g}", signed(mean_change[(s, l)]), signed(p1_change[(s, l)], 0)]
            for s in shares for l in [0.3, 0.4]]
    write_table("permutation-large", ["$f$", "Load", "Mean", "Slowest 1\\%"],
                rows, "lrrr", [2 * i for i in range(len(shares))])

    # Throughput kept per pair, by the kind of pair.
    nodes, degree = int(runs[0].options["nodes"]), int(runs[0].options["degree"])
    large = flows[flows["class"] == "large"]
    per_pair = large.groupby(["share", "load", "seed", "source_router", "destination_router", "bypass"])["goodput_gbps"].mean().unstack("bypass")
    per_pair["shortest"] = per_pair["shortest"] / per_pair["none"]
    per_pair = per_pair.reset_index()
    shape = pair_loads[(pair_loads["nodes"] == nodes) & (pair_loads["matching"] == 0) & (pair_loads["mode"] == "none")]
    shape = shape.rename(columns={"source": "source_router", "destination": "destination_router"})[
        ["seed", "source_router", "destination_router", "distance", "c"]]
    per_pair = per_pair.merge(shape, on=["seed", "source_router", "destination_router"])
    per_pair["kind"] = [pair_kind(d, c) for d, c in zip(per_pair["distance"], per_pair["c"])]

    top = per_pair[(per_pair["share"] == max(shares)) & (per_pair["load"] == per_pair["load"].max())]
    figure, axis = plt.subplots(figsize=(WIDTH, 2.4))
    grouped = top.groupby("kind")["shortest"]
    x = np.arange(len(KINDS))
    axis.bar(x, [grouped.mean()[k] * 100 for k in KINDS], width=0.5, color=common.MODE_COLORS["shortest"],
             label=f"{common.MODE_NAMES['shortest']}, mean")
    axis.scatter(x, [grouped.min()[k] * 100 for k in KINDS], color="#0b0b0b", marker="_", s=200, zorder=3,
                 label="Worst pair")
    axis.axhline(100, color="#b5b4ad", linewidth=1)
    axis.set(xticks=range(len(KINDS)), xticklabels=KINDS, ylabel="Large-flow throughput kept (%)", ylim=(50, 110),
             title=f"f = {round(100 * max(shares), 1):g}%, load {per_pair['load'].max():g}")
    axis.grid(axis="x", visible=False)
    axis.legend(ncol=2, loc="upper center", bbox_to_anchor=(0.5, -0.14), frameon=False)
    save(figure, "permutation-pairs")

    # The worst pair against the ECMP model for the same permutation (matching 0).
    model = checks[(checks["nodes"] == nodes) & (checks["matching"] == 0)]
    rows, records = [], []
    for (share, load, seed), group in per_pair.groupby(["share", "load", "seed"]):
        m = model[(model["seed"] == seed) & (model["mode"] == "shortest")].sort_values("share")
        records.append({"share": share, "load": load, "seed": seed,
                        "model": np.interp(share, m["share"], m["ecmp_kept"]), "ns3": group["shortest"].min()})
    worst = pd.DataFrame(records).groupby(["share", "load"])[["model", "ns3"]].mean()

    # One panel; the report includes it at 0.6\linewidth.
    figure, axis = plt.subplots(figsize=(0.6 * WIDTH, 2.4))
    loads = sorted(per_pair["load"].unique())
    shades = dict(zip(loads, common.setting_colors(loads).values()))
    model_line = worst.xs(loads[-1], level="load")["model"]
    axis.plot(model_line.index * 100, model_line * 100, color="#0b0b0b", linestyle="--", marker="x", label="Model (ECMP)")
    for load in loads:
        line = worst.xs(load, level="load")["ns3"]
        axis.plot(line.index * 100, line * 100, color=shades[load], marker="o", label=f"ns-3, load {load:g}")
    axis.set(xlabel="Small flows' share of bytes, f (%)", ylabel="Worst pair's throughput kept (%)", ylim=(50, 105))
    axis.legend(fontsize=7)
    save(figure, "model-vs-ns3")

    rows = []
    for share in shares:
        s = worst.loc[(share, loads[-1])]
        rows.append([pct(100 * share), pct(100 * s.model, 0), pct(100 * s.ns3, 0)])
    write_table("model-vs-ns3", ["$f$", "ECMP model", "ns-3"], rows, "lrr")


# ---------------------------------------------------------------------------------------
# 07-share-sweep: f under uniform traffic


def share_sweep():
    runs = sweep_runs("07-share-sweep")
    complete = {(r.share, r.load, r.seed) for r in runs if r.bypass == "none"} & {
        (r.share, r.load, r.seed) for r in runs if r.bypass == "shortest"}
    runs = [r for r in runs if (r.share, r.load, r.seed) in complete]
    if not runs:
        print("warning: 07-share-sweep has no complete runs yet; skipping", file=sys.stderr)
        return
    flows = common.load_flows(runs)
    links = common.load_links(runs)
    timeouts = common.timeout_table(flows, ["class", "load", "share", "bypass"])["any_timeout_pct"]
    goodput = common.goodput_table(flows, ["load", "share", "seed", "bypass"])
    # Change in each large-flow statistic, per seed (same flows in both modes), then averaged.
    change = {stat: (100 * (goodput[stat].unstack("bypass")["shortest"] / goodput[stat].unstack("bypass")["none"] - 1))
              .groupby(["load", "share"]).mean() for stat in ["mean", "p1"]}
    utilization = common.link_table(links, ["load", "share", "bypass"])["mean_utilization"].unstack("bypass")
    large = flows[flows["class"] == "large"]
    large_timeouts = (100 * (large["fct_us"] > 9000)).groupby([large["load"], large["share"], large["bypass"]]).mean()
    loads = sorted(flows["load"].unique())
    shares = sorted(flows["share"].unique())

    figure, axes = plt.subplots(1, 3, figsize=(WIDTH, 2.5))
    for load, shade in zip(loads, common.setting_colors(loads).values()):
        for axis, flow_class in zip(axes[:2], SMALL):
            for mode, style_ in [("none", ":"), ("shortest", "-")]:
                data = timeouts.xs((flow_class, load, mode), level=["class", "load", "bypass"])
                axis.plot(data.index * 100, data, color=shade, linestyle=style_, marker="o",
                          label=f"Load {load:g}, {common.MODE_NAMES[mode].lower()}")
        for stat, style_, marker in [("mean", "-", "o"), ("p1", "--", "s")]:
            data = change[stat].xs(load, level="load")
            axes[2].plot(data.index * 100, data, color=shade, linestyle=style_, marker=marker,
                         label=f"Load {load:g}, {'mean' if stat == 'mean' else 'slowest 1%'}")
    top = max(axis.get_ylim()[1] for axis in axes[:2])
    for axis, flow_class in zip(axes[:2], SMALL):
        axis.set(title=SHORT_CLASS[flow_class], xlabel="f (%)", ylim=(0, top))
    axes[0].set_ylabel("Flows with a timeout (%)")
    axes[1].tick_params(labelleft=False)
    axes[2].axhline(0, color="#b5b4ad", linewidth=1)
    axes[2].set(title="Large flows' goodput", xlabel="f (%)", ylabel="Change vs no bypass (%)")
    figure.tight_layout(w_pad=1.5)
    # One legend under the two timeout panels, one under the goodput panel.
    left, right = axes[0].get_position(), axes[1].get_position()
    for anchor, axis, columns in [((left.x0 + right.x1) / 2, axes[0], 2), (axes[2].get_position().x0 + axes[2].get_position().width / 2, axes[2], 1)]:
        handles, labels = axis.get_legend_handles_labels()
        figure.legend(handles, labels, ncol=columns, frameon=False, fontsize=7,
                      loc="upper center", bbox_to_anchor=(anchor, 0.0))
    save(figure, "uniform-share")

    rows, groups = [], []
    for load in loads:
        groups.append(len(rows))
        for share in shares:
            rows.append([f"{load:g}", pct(100 * share),
                         *[f"{timeouts[(c, load, share, 'none')]:.2f} $\\to$ {timeouts[(c, load, share, 'shortest')]:.2f}"
                           for c in SMALL],
                         f"{large_timeouts[(load, share, 'none')]:.2f} $\\to$ {large_timeouts[(load, share, 'shortest')]:.2f}",
                         f"{utilization.loc[(load, share), 'none']:.2f} $\\to$ {utilization.loc[(load, share), 'shortest']:.2f}",
                         signed(change["mean"][(load, share)]), signed(change["p1"][(load, share)])])
    write_table("uniform-share", ["Load", "$f$", "Neighbors", "Other pairs", "Large", "Util.",
                                  "Mean", "Slowest 1\\%"], rows, "rrccccrr", groups)


def main():
    FIGURES.mkdir(parents=True, exist_ok=True)
    TABLES.mkdir(parents=True, exist_ok=True)
    style()
    if available("03-load-sweep"):
        flows_four, runs_four = load_sweep()
        if available("04-oversubscription"):
            oversubscription(flows_four, runs_four)
    if available("05-bypass-model"):
        pair_loads, checks = model_checks()
        if available("06-permutation"):
            permutation(pair_loads, checks)
    if available("07-share-sweep"):
        share_sweep()


if __name__ == "__main__":
    main()
