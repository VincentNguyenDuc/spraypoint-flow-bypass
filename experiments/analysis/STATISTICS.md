# Paired p99 bootstrap

`bootstrap.py` is a separate, read-only analysis of completed H2 runs or a stable
copy of `h2-runs.csv`. It writes `intervals.csv` and `manifest.json` to a **new
directory outside the input directory**. It does not update `H2.md`, the H2 cache,
the dispatcher, or the experimental-completion checklist. Implementing this tool
does not complete the bootstrap analysis of the experimental results.

Requires Python 3.10+ and NumPy (already in `requirements.txt`). Small synthetic
tests, without running ns-3 or opening experiment outputs:

```sh
python3 -m unittest discover -s test/analysis -p 'test_bootstrap.py' -v
```

## Usage

For the inexpensive aggregate path, first take a stable copy of a completed
report CSV. Supply its path explicitly; the tool never picks the newest batch.

```sh
python3 experiments/analysis/bootstrap.py \
  --runs-csv /tmp/h2-statistics-input/h2-runs.csv \
  --output /tmp/h2-statistics-mean \
  --estimand mean-run-p99 --class small --class small_adjacent \
  --expected-seeds 561 562 563 --replicates 10000 --rng-seed 561
```

Raw inputs also support a pooled-flow p99. Loading full flow CSVs uses appreciable
memory; run this after the experiment queue finishes or restrict the conditions.
This example restricts the work to one scenario/load and one comparison:

```sh
python3 experiments/analysis/bootstrap.py \
  --batch /path/to/completed-h2-batch \
  --output /tmp/h2-statistics-pooled \
  --scenario default --load 0.5 --mode adjacent --class small \
  --estimand pooled-p99 --expected-seeds 561 562 563
```

Defaults: `mean-run-p99`, class `small`, both bypass modes, 10,000 replicates,
RNG seed 561, 95% pointwise percentile intervals. Repeat `--class`, `--scenario`,
`--load`, or `--mode` to select more populations. Classes are `all`, `small`,
`small_adjacent`, `small_other`, and `large`. The small/large classification comes
directly from the simulator's configured cutoff, including CDF workloads.

## What is estimated

Each scenario, offered load, flow class, and bypass mode produces three rows:
baseline p99, bypass p99, and bypass/baseline ratio. A ratio above 1 means slower
bypass tail FCT. Baseline estimates use the same matched cohort as that particular
bypass comparison, so with explicit exclusions their cohorts can differ.

| Estimand | Definition and weighting | Input |
| --- | --- | --- |
| `mean-run-p99` | Compute each run's p99, then take the arithmetic mean over paired runs. Every run has equal weight regardless of its flow count. With one run per seed this is the mean per-seed p99. | Raw runs or aggregate CSV |
| `pooled-p99` | Pool retained flows from the matched runs and compute their p99. Runs with more retained flows contribute more observations. | Raw runs only |

The ratio is the ratio of the two estimands computed on the **same** resample.
For `mean-run-p99`, this is the ratio of mean p99s, not the mean of paired per-run
ratios. The latter is the quantity underlying the current report's mean percent
changes; they are different and must not be relabeled as each other. Per-run
quantiles cannot recover a pooled-flow quantile, so pooled analysis rejects CSVs.

The p99 and interval endpoints use the inverse empirical CDF (smallest value
with at least the requested fraction of observations at or below it), matching
`common.percentile`. There is no interpolation between adjacent observations.
Raw runs retain starts within `[0.1 * last_start, 0.9 * last_start]`, inclusively,
matching the H2 report. `--trim` changes both ends for raw inputs only. Aggregate
inputs trust the upstream trimming and p99 calculation; those are not encoded in
the current `h2-runs.csv` schema.

## Pairing and the sampling unit

Pairs are matched on `(scenario, load, seed, run_id)`, with baseline `none`.
`run_id` is an optional aggregate-CSV field, defaulting to `0`; the raw H2 schema
has one run per seed/mode. Duplicate keys fail. Matched runs must have identical
flow fingerprints, topology fingerprints, and selected class counts. Raw input
also checks source commit and identical arguments apart from seed, bypass, and
output destination, both within each pair and across pairs in a condition. It
checks completion counts and zero exit status when recorded. Flow identity
includes flow id, routers, class flags, bytes, and start time; row order does not
affect raw-input pairing. No routing/drop-health audit is claimed by this tool.

The resampling unit is a **whole cluster of paired runs**, retaining all flows
and both modes together. Runs sharing a topology fingerprint **or seed** belong
to the same connected cluster, across all selected conditions. This preserves
traffic/topology reuse; multiple runs on one graph do not create independent
topology samples. In the usual H2 design, one seed is one topology/traffic draw.
Each replicate samples the observed number of clusters with replacement.
Repeated runs within a cluster are retained together; the estimands keep their
run or flow weighting rather than giving each topology equal outcome weight.

There is no iid resampling of flows within runs: flows interact through shared
queues. These intervals address variation across sampled seed/topology clusters,
assuming those clusters are independent, representative draws under the fixed
configuration. They do not separately identify traffic uncertainty conditional
on a fixed graph or uncertainty over untested workloads. A single shared graph
has one cluster even if it has many traffic seeds, and receives no interval.

The same cluster cohort receives the same random draws across modes, classes,
loads, and scenarios. Cohorts with different missing data get different draws.
The output intervals are still **pointwise**, with no simultaneous coverage or
formal cross-condition/crossover test. Shared draws alone do not establish a
significance test for differences between conditions.

## Missing data and unfinished flows

Raw `.partial` and `.failed` directories are ignored and listed in the manifest.
A malformed finalized run fails. Missing paired modes fail by default.
`--allow-incomplete` explicitly excludes unmatched runs and records each missing
pair; fingerprint/settings mismatches always fail. Exclusions can create
selection bias, especially when difficult runs are unfinished or failed.

Use `--expected-seeds` to detect seeds absent from both modes within observed
conditions. Without it, only seeds present in an input are known. Neither mode
infers an entirely absent scenario/load or certifies completion of the full H2
matrix. Finalized simulation directories may contain unfinished **flows**; this
is distinct from an unfinished run directory. Zero retained flows in a selected
class fail, rather than silently changing the estimand by dropping runs.

Unfinished flows have `+inf` FCT, as in the H2 report. This treats them as worst
outcomes and can make the p99 or an interval endpoint infinite. It is a censoring
sentinel, not an estimate of their eventual completion time; these intervals are
not a survival-analysis correction. Ratios with a nonfinite input or zero
baseline are undefined. If the point estimate or **any** bootstrap ratio is
undefined, the ratio interval is withheld; invalid draws are counted, never
discarded and renormalized. `inf` appears as a string in strict JSON; undefined
numbers are JSON `null` or empty CSV cells. Full-run unfinished counts are
reported separately from the trimmed, class-specific analyzed-flow counts.

No FCT threshold is used to infer timeout/handshake classes or remove flows.
Long large/CDF flows remain in their populations. This command estimates raw
FCT p99s; it does not provide a “clean p99” without direct timeout instrumentation.

## Finite-sample limits and reproducibility

One cluster has no interval. Fewer than ten clusters receive the
`exploratory_few_clusters` status; ten is a reporting heuristic, not a boundary
at which nominal coverage becomes reliable. With the current **three seeds**,
there are only ten unordered cluster-multiplicity combinations. Thousands of
bootstrap replicates refine sampling of those combinations; they create no new
topologies, tail events, or independent evidence. Percentile intervals can be
coarse, narrow, degenerate, or poorly calibrated. They cannot confirm H2 or a
crossover with three seeds, even when they exclude ratio 1. The general warning
about few independent clusters is discussed by
[Cameron and Miller (2015)](https://cameron.econ.ucdavis.edu/research/Cameron_Miller_JHR_2015.pdf);
that regression paper does not validate this p99 procedure.

`min_run_beyond_p99` reports `floor(retained_class_flows / 100)` for the smallest
matched run. Fewer than about 100 nominal tail observations deserves particular
caution. This is a sample-size diagnostic, not a count of strictly larger values
when there are ties and not an effective number of independent observations.
More flows cannot replace more independent seeds. A zero-width interval is
marked `degenerate_empirical_interval`, not evidence of zero uncertainty.

Intervals are two-sided percentile bootstrap intervals, as described in the
[SciPy bootstrap documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.stats.bootstrap.html),
applied to cluster resamples here rather than independent flow samples. This
implementation uses only NumPy and the standard library, not SciPy. It caches
statistics for duplicate multiplicity vectors and uses weighted order statistics
for pooled p99, avoiding literal copies of resampled flow arrays.

`--rng-seed` and a stable hash of the sorted cluster cohort initialize PCG64 via
SeedSequence. Input order does not affect the output. The manifest records RNG
configuration, Python/NumPy versions, CSV content hash or raw file size/mtime
evidence, matched cohorts, omissions, and all estimates. Raw and aggregate
fingerprint encodings differ, so their random streams need not be identical.
Exact resampling reproduction assumes the same inputs, selected cohort, settings,
and NumPy version. Input changes during loading fail; take stable snapshots when
another process is replacing aggregate CSVs.
