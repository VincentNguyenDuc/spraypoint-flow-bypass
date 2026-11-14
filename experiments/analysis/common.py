"""Loads experiment results from one batch,
experiments/results/<machine>/<batch>/<experiment>/<run>/, for the notebooks."""

import os
import re
from dataclasses import dataclass, field
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[2]
RESULTS_ROOT = ROOT / "experiments" / "results"

# The sweep the notebooks analyze: 03-load-sweep, which keeps the full-scale design's
# proportions, or 02-small-sweep, 04-oversubscription, 06-permutation, 07-share-sweep,
# or 01-smoke for a quick check that the notebooks run, though its runs are too short for tail numbers.
SWEEP = os.environ.get("SWEEP", "03-load-sweep")
BASELINE_EXPERIMENT = f"{SWEEP}-baseline"
BYPASS_EXPERIMENT = f"{SWEEP}-bypass"

# What varies between a sweep's runs besides the seed and the bypass mode.  Every table
# groups by all of SETTINGS, and every plot puts the last one, SETTING, on its x-axis,
# with one panel per value of the others (see panels).  Most sweeps vary the offered
# load.  04-oversubscription holds the load at 0.5 and varies the hosts per router,
# which makes the fabric busier while each host link stays at the same load.
# 06-permutation and 07-share-sweep vary both the small flows' share of bytes and the
# load, under permutation and uniform traffic.
SETTINGS = {
    "04-oversubscription": ["hosts"],
    "06-permutation": ["share", "load"],
    "07-share-sweep": ["share", "load"],
}.get(SWEEP, ["load"])
SETTING = SETTINGS[-1]
SETTING_LABELS = {"load": "Offered load", "hosts": "Hosts per router", "share": "Small flows' share of bytes"}
SETTING_LABEL = SETTING_LABELS[SETTING]


def setting_name(value, setting=SETTING):
    """A label for one setting, such as "Load 0.5", "6 hosts per router" or "Small flows
    20% of bytes"."""
    if setting == "load":
        return f"Load {value:g}"
    if setting == "hosts":
        return f"{value:g} hosts per router"
    return f"Small flows {100 * value:.3g}% of bytes"


def panels(frame):
    """(title, rows) for each combination of the settings other than SETTING, so that each
    plot shows one setting on its x-axis.  With one setting, a single panel titled None."""
    outer = SETTINGS[:-1]
    if not outer:
        yield None, frame
        return
    for values, rows in frame.groupby(outer):
        values = values if isinstance(values, tuple) else (values,)
        yield ", ".join(setting_name(v, s) for s, v in zip(outer, values)), rows


def label_setting_axis(axis):
    """Labels a plot's x-axis with the setting, in whole hosts when it counts hosts."""
    axis.set_xlabel(SETTING_LABEL)
    if SETTING == "hosts":
        axis.xaxis.set_major_locator(plt.MaxNLocator(integer=True))


def _find_batch():
    """The batch to read: RESULTS_DIR if set, otherwise the newest batch with this sweep,
    from the machine in RESULTS_MACHINE ("local" or "remote") if set, and the batch in
    RESULTS_BATCH if set.  Batch names start with the date, so the newest sorts last."""
    if "RESULTS_DIR" in os.environ:
        return Path(os.environ["RESULTS_DIR"])
    machine = os.environ.get("RESULTS_MACHINE", "*")
    batch = os.environ.get("RESULTS_BATCH", "*")
    found = [path for path in RESULTS_ROOT.glob(f"{machine}/{batch}") if (path / BASELINE_EXPERIMENT).is_dir()]
    if not found:
        return RESULTS_ROOT / "no-batch-found"
    return max(found, key=lambda path: (path.name, path.parent.name))


RESULTS = _find_batch()
MACHINE, BATCH = RESULTS.parent.name, RESULTS.name

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
SMALL_CLASSES = ["small_adjacent", "small_other"]

# The handshake timeout is this many minimum RTOs: see ConnTimeout in
# src/tcp/tcp-experiment.cc.
CONN_TIMEOUT_RTOS = 10


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
    def hosts(self):
        # The simulator's default, for runs that don't set it.
        return int(self.options.get("hostsPerRouter", 1))

    @property
    def share(self):
        """f, the small flows' share of the bytes offered, for the two-size workload.  It
        is the share the bypass moves, so it is what the models in
        docs/bypass-modelling.tex call f.  NaN for a flow-size CDF."""
        if "flowSizeCdf" in self.options:
            return float("nan")
        # The simulator's defaults, for runs that don't set them.
        small = float(self.options.get("smallBytes", 10_000))
        large = float(self.options.get("largeBytes", 1_000_000))
        fraction = float(self.options.get("smallFraction", 0.9))
        return round(fraction * small / (fraction * small + (1 - fraction) * large), 4)

    @property
    def setting(self):
        """This run's values of SETTINGS: one value, or a tuple when there are several."""
        values = tuple(getattr(self, s) for s in SETTINGS)
        return values[0] if len(values) == 1 else values

    @property
    def seed(self):
        return int(self.options["seed"])

    @property
    def bypass(self):
        return self.options.get("bypass", "none")


def _parse_summary(text):
    summary = {}
    summary["min_rto_us"] = float(re.search(r"min-rto-us=(\S+)", text)[1])
    match = re.search(r"completed=(\d+)/(\d+) .*sim_end_us=(\S+)", text)
    summary["completed"], summary["flows"] = int(match[1]), int(match[2])
    summary["sim_end_us"] = float(match[3])
    summary["link_gbps"] = float(re.search(r"link-gbps=(\S+)", text)[1])
    summary["fabric_queue"] = int(re.search(r"fabric-queue=(\d+)", text)[1])
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
    """Finished runs of one experiment, sorted by setting, seed, and bypass mode."""
    runs = []
    for path in sorted((RESULTS / experiment).glob("*")):
        # A run is only finished once common.sh renames it to its final name.
        if path.suffix in (".partial", ".failed") or not (path / "summary.txt").is_file():
            continue
        options = dict(
            line.lstrip("-").split("=", 1) for line in (path / "args.txt").read_text().split()
        )
        meta = dict(line.split(": ", 1) for line in (path / "meta.txt").read_text().splitlines())
        meta["peak_memory_mb"] = _peak_memory_mb(path / "resources.log")
        runs.append(Run(path.name, path, options, meta, _parse_summary((path / "summary.txt").read_text())))
    return sorted(runs, key=lambda r: (r.setting, r.seed, MODES.index(r.bypass)))


# The flow-level check of the bypass models (test/bypass-model), in the same batch.
MODEL_EXPERIMENT = "05-bypass-model"


def load_model_checks():
    """The finished runs of MODEL_EXPERIMENT in this batch, as two tables tagged with each
    run's nodes, degree and seed: oversubscription per permutation, bypass mode and share,
    and each pair's busiest links under ECMP.  Matching 0 of a seed is the permutation that
    06-permutation simulates with that seed and network size."""
    oversubscription, pairs = [], []
    for path in sorted((RESULTS / MODEL_EXPERIMENT).glob("*")):
        if path.suffix in (".partial", ".failed") or not (path / "summary.txt").is_file():
            continue
        options = dict(line.lstrip("-").split("=", 1) for line in (path / "args.txt").read_text().split())
        tags = {"nodes": int(options["nodes"]), "degree": int(options["degree"]), "seed": int(options["seed"])}
        oversubscription.append(pd.read_csv(path / "oversubscription.csv").assign(**tags))
        pairs.append(pd.read_csv(path / "pairs.csv").assign(**tags))
    if not oversubscription:
        return pd.DataFrame(), pd.DataFrame()
    return pd.concat(oversubscription, ignore_index=True), pd.concat(pairs, ignore_index=True)


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
                "hosts": run.hosts,
                "share": run.share,
                "seed": run.seed,
                "flows": run.summary["flows"],
                "unfinished": run.summary["flows"] - run.summary["completed"],
                "ttl_drops": drops["ttl"],
                "no_route_drops": drops["no_route"],
                "queue_drops": drops["router_queue"] + drops["host_queue"],
                "wall_minutes": round(float(run.meta.get("wall_seconds", "nan")) / 60, 1),
                "peak_memory_mb": round(run.meta["peak_memory_mb"], 1),
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
        flows["goodput_gbps"] = flows["bytes"] * 8 / (flows["fct_us"] * 1e3)
        flows["timeout"] = _timeouts(flows, run.summary["min_rto_us"])
        flows["load"], flows["hosts"], flows["share"] = run.load, run.hosts, run.share
        flows["seed"], flows["bypass"] = run.seed, run.bypass
        frames.append(flows)
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def _timeouts(flows, min_rto_us):
    """Which timeout, if any, a small flow waited for: "handshake" for a lost SYN or
    SYN-ACK, "data" for a retransmission timeout, or "none".

    An FCT of at least the minimum RTO means a timeout, since a small flow that loses
    nothing finishes in well under a millisecond: in the sweeps none take between 0.5
    and 1 ms.  Large flows can take longer than an RTO from queueing alone, so they get
    "unknown" unless they waited for the handshake timeout."""
    fct = flows["fct_us"]
    small = flows["small"] == 1
    return np.select(
        [~np.isfinite(fct), fct >= CONN_TIMEOUT_RTOS * min_rto_us, ~small, fct >= min_rto_us],
        ["unfinished", "handshake", "unknown", "data"],
        "none",
    )


def timeout_table(flows, by):
    """For small flows: the percentage that waited for each kind of timeout, and FCT
    percentiles in microseconds of those that did not, which is the queueing alone."""

    def describe(group):
        timeout = group["timeout"]
        clean = group.loc[timeout == "none", "fct_us"].to_numpy()
        return pd.Series(
            {
                "flows": len(group),
                "data_timeout_pct": 100 * (timeout == "data").mean(),
                "handshake_timeout_pct": 100 * (timeout == "handshake").mean(),
                "any_timeout_pct": 100 * timeout.isin(["data", "handshake", "unfinished"]).mean(),
                "clean_p50": percentile(clean, 0.50) if len(clean) else np.nan,
                "clean_p99": percentile(clean, 0.99) if len(clean) else np.nan,
                "clean_max": clean.max() if len(clean) else np.nan,
            }
        )

    small = flows[flows["class"].isin(SMALL_CLASSES)]
    table = small.groupby(by, sort=True).apply(describe, include_groups=False)
    return table.astype({"flows": int})


def seed_change_table(flows):
    """How consistently each bypass mode changes small flows, seed by seed: each seed's
    bypass run against the baseline run with the same flows.  Timeout changes are in
    percentage points, since a baseline can have none; FCT changes are in percent."""
    per_seed = timeout_table(flows, ["class", *SETTINGS, "seed", "bypass"])
    baseline = per_seed.xs("none", level="bypass")
    rows = []
    for mode in [m for m in MODES if m != "none" and m in per_seed.index.get_level_values("bypass")]:
        other = per_seed.xs(mode, level="bypass")
        change = pd.DataFrame(
            {
                "timeout_pts": other["any_timeout_pct"] - baseline["any_timeout_pct"],
                "fewer_timeouts": other["any_timeout_pct"] < baseline["any_timeout_pct"],
                "clean_p50_pct": 100 * (other["clean_p50"] / baseline["clean_p50"] - 1),
                "clean_p99_pct": 100 * (other["clean_p99"] / baseline["clean_p99"] - 1),
            }
        )
        grouped = change.groupby(["class", *SETTINGS])
        table = pd.DataFrame(
            {
                "seeds": grouped.size(),
                "seeds_fewer_timeouts": grouped["fewer_timeouts"].sum(),
                "timeout_pts_mean": grouped["timeout_pts"].mean(),
                "timeout_pts_min": grouped["timeout_pts"].min(),
                "timeout_pts_max": grouped["timeout_pts"].max(),
                "clean_p50_pct_mean": grouped["clean_p50_pct"].mean(),
                "clean_p99_pct_mean": grouped["clean_p99_pct"].mean(),
                "clean_p99_pct_min": grouped["clean_p99_pct"].min(),
                "clean_p99_pct_max": grouped["clean_p99_pct"].max(),
            }
        )
        rows.append(table.assign(bypass=mode).set_index("bypass", append=True))
    return pd.concat(rows).sort_index().astype({"seeds": int, "seeds_fewer_timeouts": int})


def mean_hops_table(runs):
    """Mean fabric hops of delivered packets, including ACKs, by class, mode and setting."""
    paths = path_table(runs)
    paths["weighted"] = paths["hops"] * paths["fraction"]
    per_run = paths.groupby(["class", "bypass", *SETTINGS, "seed"])["weighted"].sum()
    return per_run.groupby(["class", "bypass", *SETTINGS]).mean().unstack(SETTINGS)


def goodput_table(flows, by):
    """Large-flow goodput in Gbps: each flow's bytes over its FCT.  The low percentiles
    are the slowest flows, which a hotspot would hurt first."""
    large = flows[flows["class"] == "large"]

    def describe(group):
        goodput = group["goodput_gbps"].to_numpy()
        return pd.Series(
            {
                "flows": len(goodput),
                "mean": goodput.mean(),
                "p50": percentile(goodput, 0.50),
                "p10": percentile(goodput, 0.10),
                "p1": percentile(goodput, 0.01),
            }
        )

    table = large.groupby(by, sort=True).apply(describe, include_groups=False)
    return table.astype({"flows": int})


def traffic_mix(flows, by):
    """Each class's share of flows and of bytes."""
    totals = flows.groupby(list(by) + ["class"])["bytes"].agg(flows="count", bytes="sum")
    return totals / totals.groupby(list(by)).transform("sum")


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


def load_links(runs):
    """One row per directed fabric link, with its utilization while flows kept arriving,
    which is when the network carries the offered load, and whether its queue ever filled."""
    frames = []
    for run in runs:
        links = pd.read_csv(run.path / "links.csv")
        last_start_ns = pd.read_csv(run.path / "flows.csv", usecols=["start_ns"])["start_ns"].max()
        capacity_bytes = run.summary["link_gbps"] * 1e9 / 8 * last_start_ns * 1e-9
        links["utilization"] = links["sent_bytes_during_arrivals"] / capacity_bytes
        links["filled"] = links["max_queue_packets"] >= run.summary["fabric_queue"]
        links["load"], links["hosts"], links["share"] = run.load, run.hosts, run.share
        links["seed"], links["bypass"] = run.seed, run.bypass
        frames.append(links)
    return pd.concat(frames, ignore_index=True) if frames else pd.DataFrame()


def link_table(links, by):
    """How evenly traffic spreads across links."""
    return links.groupby(by).agg(
        mean_utilization=("utilization", "mean"),
        p99_utilization=("utilization", lambda u: percentile(u.to_numpy(), 0.99)),
        max_utilization=("utilization", "max"),
        hotspot_ratio=("utilization", lambda u: u.max() / u.mean()),
        share_of_links_filled=("filled", "mean"),
        mean_queue_packets=("mean_queue_packets_during_arrivals", "mean"),
    )


def link_changes(links):
    """Each bypass run's links paired with the same link in the baseline run at the same
    setting and seed.  The topology depends only on the seed, so a link is the same link in
    every mode."""
    keys = [*SETTINGS, "seed", "from", "to"]
    columns = ["utilization", "filled", "sent_small_bytes", "max_queue_packets"]
    baseline = links[links["bypass"] == "none"][keys + columns]
    paired = links[links["bypass"] != "none"][keys + ["bypass"] + columns].merge(
        baseline, on=keys, suffixes=("", "_baseline")
    )
    paired["utilization_change"] = paired["utilization"] - paired["utilization_baseline"]
    return paired


def hotspot_table(paired, by, top_share=0.01):
    """The links the bypass made busiest: the top_share of links by utilization gain.
    If the bypass creates hotspots, these links are busier than the baseline's busiest
    and their queues fill more often than before."""

    def describe(group):
        top = group.nlargest(max(1, round(top_share * len(group))), "utilization_change")
        return pd.Series(
            {
                "links_busier": (group["utilization_change"] > 0.01).mean(),
                "links_quieter": (group["utilization_change"] < -0.01).mean(),
                "largest_gain": group["utilization_change"].max(),
                "top_utilization_before": top["utilization_baseline"].mean(),
                "top_utilization_after": top["utilization"].mean(),
                "top_filled_before": top["filled_baseline"].mean(),
                "top_filled_after": top["filled"].mean(),
            }
        )

    return paired.groupby(by, sort=True).apply(describe, include_groups=False)


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
        {"bypass": run.bypass, "load": run.load, "hosts": run.hosts, "share": run.share, "seed": run.seed, "class": name, "hops": hops, "fraction": fraction}
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


def setting_colors(values):
    """Light to dark by setting, since load and hosts per router are both ordered."""
    ramp = ["#86b6ef", "#5598e7", "#2a78d6", "#1c5cab", "#104281"]
    values = sorted(set(values))
    if len(values) == 1:
        return {values[0]: ramp[2]}
    return {value: ramp[round(i * (len(ramp) - 1) / (len(values) - 1))] for i, value in enumerate(values)}


def tail_curve(values):
    """Sorted finite values, and the share of flows slower than each."""
    values = np.sort(values)
    share_slower = 1 - np.arange(1, len(values) + 1) / len(values)
    finite = np.isfinite(values) & (share_slower > 0)
    return values[finite], share_slower[finite]


def plot_metric_by_mode(flows, metric, ylabel, table=fct_table, classes=CLASSES):
    """A metric from table against the setting, one line per bypass mode and one panel per flow
    class, in one figure per panel (see panels).  Lines show the mean across seeds, and bars
    show the lowest and highest seed."""
    per_seed = table(flows, ["class", "bypass", *SETTINGS, "seed"])[metric].reset_index()
    figures = []
    for title, rows in panels(per_seed):
        figure, axes = plt.subplots(1, len(classes), figsize=(3.7 * len(classes), 3.4), sharex=True)
        for axis, flow_class in zip(np.atleast_1d(axes), classes):
            for mode in MODES:
                data = rows[(rows["class"] == flow_class) & (rows["bypass"] == mode)]
                if data.empty:
                    continue
                spread = data.groupby(SETTING)[metric].agg(["mean", "min", "max"])
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
            label_setting_axis(axis)
            axis.set_ylim(bottom=0)
        axes = np.atleast_1d(axes)
        axes[0].set_ylabel(ylabel)
        axes[0].legend()
        if title:
            figure.suptitle(title)
        figure.tight_layout()
        figures.append(figure)
    return figures
