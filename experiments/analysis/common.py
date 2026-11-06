"""Loads experiment results from experiments/results/<experiment>/<run>/ for the notebooks."""

import os
import re
from dataclasses import dataclass, field
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
RESULTS = Path(os.environ.get("RESULTS_DIR", ROOT / "experiments" / "results"))

BASELINE_EXPERIMENT = os.environ.get("BASELINE_EXPERIMENT", "tcp-baseline-load-sweep")
BYPASS_EXPERIMENT = os.environ.get("BYPASS_EXPERIMENT", "tcp-bypass-load-sweep")

# Flows starting this close to either end of the run are left out by default: the
# network starts empty and drains at the end, so they see less load.
TRIM_FRACTION = 0.1

CLASSES = ["small_adjacent", "small_other", "large"]
CLASS_NAMES = {
    "small_adjacent": "Small, adjacent routers",
    "small_other": "Small, other routers",
    "large": "Large",
}
MODES = ["none", "adjacent", "shortest"]
MODE_NAMES = {"none": "No bypass", "adjacent": "Adjacent bypass", "shortest": "Shortest-path bypass"}
MODE_COLORS = {"none": "#2a78d6", "adjacent": "#eb6834", "shortest": "#1baf7a"}
MODE_MARKERS = {"none": "o", "adjacent": "s", "shortest": "^"}


@dataclass
class Run:
    name: str
    path: Path
    options: dict
    meta: dict
    summary: dict = field(repr=False)

    @property
    def load(self):
        return float(self.options["load"])

    @property
    def seed(self):
        return int(self.options["seed"])

    @property
    def bypass(self):
        return self.options.get("bypass", "none")


def _parse_summary(text):
    summary = {}
    match = re.search(r"completed=(\d+)/(\d+) .*sim_end_us=(\S+)", text)
    summary["completed"], summary["flows"] = int(match[1]), int(match[2])
    summary["sim_end_us"] = float(match[3])
    summary["link_gbps"] = float(re.search(r"link-gbps=(\S+)", text)[1])
    summary["drops"] = {k: int(v) for k, v in re.findall(r"(\w+)=(\d+)", re.search(r"^drops (.*)$", text, re.M)[1])}
    summary["path_lengths"] = {
        name: {int(h): float(f) for h, f in re.findall(r"L(\d+)=(\S+)", line)}
        for name, line in re.findall(r"^path_length_fraction_(\w+) (.*)$", text, re.M)
    }
    summary["fct_us"] = {
        name: {k: float(v) for k, v in re.findall(r"(p50|p99|p999|max)=(\S+)", line)}
        for name, line in re.findall(r"^fct_us class=(\w+) (.*)$", text, re.M)
    }
    return summary


def _peak_memory_mb(path):
    if not path.exists():
        return np.nan
    text = path.read_text()
    macos = re.search(r"(\d+)\s+maximum resident set size", text)
    if macos:
        return int(macos[1]) / 1e6
    linux = re.search(r"Maximum resident set size \(kbytes\): (\d+)", text)
    return int(linux[1]) / 1e3 if linux else np.nan


def load_runs(experiment):
    """Finished runs of one experiment, sorted by load, seed, and bypass mode."""
    runs = []
    for path in sorted((RESULTS / experiment).glob("*")):
        if not (path / "summary.txt").is_file():
            continue
        options = dict(
            line.lstrip("-").split("=", 1) for line in (path / "args.txt").read_text().split()
        )
        meta = dict(line.split(": ", 1) for line in (path / "meta.txt").read_text().splitlines())
        meta["peak_memory_mb"] = _peak_memory_mb(path / "resources.log")
        runs.append(Run(path.name, path, options, meta, _parse_summary((path / "summary.txt").read_text())))
    return sorted(runs, key=lambda r: (r.load, r.seed, MODES.index(r.bypass)))


def runs_table(runs):
    """One row per run, for checking that the runs are healthy."""
    rows = []
    for run in runs:
        drops = run.summary["drops"]
        rows.append(
            {
                "run": run.name,
                "bypass": run.bypass,
                "load": run.load,
                "seed": run.seed,
                "flows": run.summary["flows"],
                "unfinished": run.summary["flows"] - run.summary["completed"],
                "ttl_drops": drops["ttl"],
                "no_route_drops": drops["no_route"],
                "queue_drops": drops["router_queue"] + drops["host_queue"],
                "wall_minutes": round(float(run.meta.get("wall_seconds", "nan")) / 60, 1),
                "peak_memory_mb": round(run.meta["peak_memory_mb"]),
                "commit": run.meta.get("commit", ""),
            }
        )
    return pd.DataFrame(rows)


def load_flows(runs, trim=TRIM_FRACTION):
    """One row per flow, with fct_us.  An unfinished flow has an infinite FCT, so it
    counts as the worst flow in a percentile instead of disappearing."""
    frames = []
    for run in runs:
        flows = pd.read_csv(run.path / "flows.csv")
        span_ns = flows["start_ns"].max()
        flows = flows[flows["start_ns"].between(trim * span_ns, (1 - trim) * span_ns)].copy()
        flows["fct_us"] = (flows["finish_ns"] - flows["start_ns"]) / 1e3
        flows["fct_us"] = flows["fct_us"].fillna(np.inf)
        flows["class"] = np.select(
            [(flows["small"] == 1) & (flows["adjacent"] == 1), flows["small"] == 1],
            ["small_adjacent", "small_other"],
            "large",
        )
        flows["load"], flows["seed"], flows["bypass"] = run.load, run.seed, run.bypass
        frames.append(flows)
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def percentile(values, q):
    """The simulator's definition: the smallest value with at least q of the data at or
    below it."""
    return np.percentile(values, q * 100, method="inverted_cdf")


def fct_table(flows, by):
    """Flow counts and FCT percentiles in microseconds, pooling everything in each group."""

    def describe(group):
        fct = group["fct_us"].to_numpy()
        return pd.Series(
            {
                "flows": len(fct),
                "unfinished": int(np.isinf(fct).sum()),
                "p50": percentile(fct, 0.50),
                "p99": percentile(fct, 0.99),
                "p99.9": percentile(fct, 0.999),
                "beyond_p99": int(len(fct) * 0.01),
            }
        )

    table = flows.groupby(by, sort=True).apply(describe, include_groups=False)
    return table.astype({"flows": int, "unfinished": int, "beyond_p99": int})


def _active_seconds(run):
    """How long the run carried traffic: until flows holding 99% of the bytes finished.
    The simulated end time doesn't work here, since one lost SYN can extend it far past
    the traffic, and a flow-count percentile can land on those stragglers too."""
    flows = pd.read_csv(run.path / "flows.csv", usecols=["bytes", "finish_ns"])
    flows["finish_ns"] = flows["finish_ns"].fillna(np.inf)
    flows = flows.sort_values("finish_ns")
    reached = flows["bytes"].cumsum() >= 0.99 * flows["bytes"].sum()
    return flows.loc[reached.idxmax(), "finish_ns"] * 1e-9


def load_links(runs):
    """One row per directed fabric link, with its utilization while the run carried
    traffic, and its mean queue over that same time."""
    frames = []
    for run in runs:
        links = pd.read_csv(run.path / "links.csv")
        forwarded = links["forwarded_small_bytes"] + links["forwarded_large_bytes"]
        active = _active_seconds(run)
        links["utilization"] = forwarded / (run.summary["link_gbps"] * 1e9 / 8 * active)
        # The simulator averages over the whole run; queues are empty once traffic stops.
        links["mean_queue_packets"] *= run.summary["sim_end_us"] * 1e-6 / active
        links["load"], links["seed"], links["bypass"] = run.load, run.seed, run.bypass
        frames.append(links)
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def link_table(links, by):
    """How evenly traffic spreads across links."""
    return links.groupby(by).agg(
        mean_utilization=("utilization", "mean"),
        p99_utilization=("utilization", lambda u: percentile(u.to_numpy(), 0.99)),
        max_utilization=("utilization", "max"),
        hotspot_ratio=("utilization", lambda u: u.max() / u.mean()),
        links_at_full_queue=("max_queue_packets", lambda q: int((q >= q.max()).sum())),
        mean_queue_packets=("mean_queue_packets", "mean"),
    )


def fan_in_table(links, by):
    """How each destination's traffic arrives: the busiest neighbor's share of it, for a
    fan-in across many neighbors this is small."""
    delivered = links.assign(
        delivered=links["delivered_small_bytes"] + links["delivered_large_bytes"]
    )
    keys = list(by) + ["seed", "to"]
    per_destination = delivered.groupby(keys)["delivered"].agg(
        total="sum",
        busiest="max",
        neighbors=lambda d: int((d > 0).sum()),
    )
    per_destination = per_destination[per_destination["total"] > 0]
    per_destination["busiest_share"] = per_destination["busiest"] / per_destination["total"]
    return per_destination.groupby(list(by))[["busiest_share", "neighbors"]].mean()


def path_table(runs):
    """Share of delivered packets at each fabric hop count."""
    rows = [
        {"bypass": run.bypass, "load": run.load, "seed": run.seed, "class": name, "hops": hops, "fraction": fraction}
        for run in runs
        for name, lengths in run.summary["path_lengths"].items()
        for hops, fraction in lengths.items()
    ]
    return pd.DataFrame(rows)


def setup_plots():
    plt.rcParams.update(
        {
            "figure.dpi": 110,
            "figure.facecolor": "#fcfcfb",
            "axes.facecolor": "#fcfcfb",
            "axes.edgecolor": "#b5b4ad",
            "axes.labelcolor": "#52514e",
            "axes.titlecolor": "#0b0b0b",
            "axes.titlesize": 11,
            "axes.labelsize": 10,
            "axes.grid": True,
            "axes.axisbelow": True,
            "axes.spines.top": False,
            "axes.spines.right": False,
            "grid.color": "#e6e5e0",
            "grid.linewidth": 0.8,
            "xtick.color": "#52514e",
            "ytick.color": "#52514e",
            "xtick.labelsize": 9,
            "ytick.labelsize": 9,
            "legend.frameon": False,
            "legend.fontsize": 9,
            "lines.linewidth": 2,
            "lines.markersize": 7,
        }
    )


def load_colors(loads):
    """Light to dark by load, since load is ordered."""
    ramp = ["#86b6ef", "#5598e7", "#2a78d6", "#1c5cab", "#104281"]
    loads = sorted(set(loads))
    if len(loads) == 1:
        return {loads[0]: ramp[2]}
    return {load: ramp[round(i * (len(ramp) - 1) / (len(loads) - 1))] for i, load in enumerate(loads)}


def tail_curve(values):
    """Sorted finite values, and the share of flows slower than each."""
    values = np.sort(values)
    share_slower = 1 - np.arange(1, len(values) + 1) / len(values)
    finite = np.isfinite(values) & (share_slower > 0)
    return values[finite], share_slower[finite]


def plot_metric_by_mode(flows, metric, title):
    """A percentile against load, one line per bypass mode and one panel per flow class.
    Lines show the mean across seeds, and bars show the lowest and highest seed."""
    per_seed = fct_table(flows, ["class", "bypass", "load", "seed"])[metric].reset_index()
    figure, axes = plt.subplots(1, len(CLASSES), figsize=(11, 3.4), sharex=True)
    for axis, flow_class in zip(axes, CLASSES):
        for mode in MODES:
            data = per_seed[(per_seed["class"] == flow_class) & (per_seed["bypass"] == mode)]
            if data.empty:
                continue
            spread = data.groupby("load")[metric].agg(["mean", "min", "max"])
            axis.errorbar(
                spread.index,
                spread["mean"],
                yerr=[spread["mean"] - spread["min"], spread["max"] - spread["mean"]],
                color=MODE_COLORS[mode],
                marker=MODE_MARKERS[mode],
                label=MODE_NAMES[mode],
                capsize=3,
            )
        axis.set_title(CLASS_NAMES[flow_class])
        axis.set_xlabel("Offered load")
        axis.set_ylim(bottom=0)
    axes[0].set_ylabel(f"{title} (µs)")
    axes[0].legend()
    figure.tight_layout()
    return figure
