# H3 routing and threshold tradeoff reports

`h3_report.py` reads a stable `h2-runs.csv` aggregate and produces an exploratory
comparison of baseline (`none`), adjacent bypass, and shortest-path bypass.
It uses only the Python standard library. It does not run simulations, access
raw experiment directories, modify an H2 cache, or establish completion of an
H3 or cutoff sweep.

## Run it against a stable snapshot

```sh
python3 experiments/analysis/h3_report.py /path/to/batch/analysis/h2-runs.csv \
    --output-dir /tmp/h3-review-001 --expected-seeds 561 562 563
```

The output directory is required, must not exist, and must have an existing
parent. It must be outside the input batch, including through symlinks. For
the standard `batch/analysis/h2-runs.csv` layout, the protected batch root is
`batch`. Otherwise, the default protected root is the CSV's parent. Supply
`--input-batch /path/to/batch` when the CSV lives in a differently named
subdirectory. An explicit root cannot weaken protection of the inferred root.
Existing reports are never overwritten.

Use an immutable snapshot or a completed batch's stable aggregate. The tool
checks file identity, size, and modification/change timestamps around reading
and again before writing output. It records the SHA-256 of the exact input
bytes and of the report script. This detects ordinary concurrent updates but
does not lock a scheduler or certify an entire batch as complete.

Every observed `(scenario, load, seed)` must have all three distinct mode rows.
`--expected-seeds` additionally requires those seeds for every observed
scenario/load. The optional flag `--allow-incomplete` explicitly excludes
entire groups missing any mode and records the exclusions. It never fills in
missing results or compares a two-mode subset. If every group is excluded, the
report contains no comparisons and says so. A standalone CSV cannot reveal
entirely absent scenarios, loads, or unrequested seeds.

## Validation and source assumptions

The input schema is the one emitted by `experiments/analysis/h2-report.py`.
Required fields include keys, flow/topology fingerprints, total/analyzed/class
population counts, unfinished counts, small and adjacent-small byte shares,
all-small/adjacent-small/other-small p50 and p99, and large mean/p01 goodput.
Fabric utilization, hotspot ratio, busiest-neighbor share, queue measurements,
drop counts, and large p99 are included when present. Optional columns are not
required merely to produce the core routing report.

Validation rejects duplicate keys, invalid fingerprints, mismatched flow or
topology fingerprints, inconsistent class partitions, mismatched populations,
and mismatched byte shares across compared modes. Population identities are:

- `all_flows = analyzed_flows <= flows`;
- `small_flows + large_flows = all_flows`;
- `small_adjacent_flows + small_other_flows = small_flows`;
- `0 <= unfinished <= flows`.

Unfinished counts may differ by routing mode because they are outcomes.
Present `*_beyond_p99` fields must equal `floor(class_flows / 100)`. Empty
small classes must have undefined quantiles; an empty large class must have
undefined goodputs. An empty analyzed window is retained with explicit
undefined metrics and no frontier. Negative metrics and nonfinite load/byte
shares are rejected. NaN metrics and positive infinite metrics are retained,
flagged, and excluded from dependent ratio/frontier claims.

The tool compares fingerprints already in the aggregate; it cannot recompute
them or verify upstream TCP run settings without raw data. It trusts the H2
producer's empirical quantile calculation, middle-80%-of-arrivals selection,
and completion bookkeeping. That producer encodes unfinished FCTs as positive
infinity and their goodputs as zero. The report does not infer timeouts from
FCT and does not treat observed data retransmissions as handshake timeouts.

## Quantities and interpretations

The reported all-small p50 and p99 are exactly `small_p50_us` and
`small_p99_us` from the input. They are quantiles over the full selected small
population. The report never averages adjacent and non-adjacent quantiles to
construct an all-small quantile. Adjacent and non-adjacent p50/p99 remain
separate diagnostic results.

Large mean goodput is the arithmetic mean of per-flow goodputs in Gbit/s;
slow-tail goodput is their first percentile. Higher values are better. These
are not aggregate link throughput or bytes divided by total elapsed time.
Hotspot ratio is maximum/mean directed fabric-link utilization. Mean
busiest-neighbor share describes how concentrated delivered incoming fabric
bytes are among neighbors of a receiver. Neither is an instantaneous number
of simultaneous senders. Link utilization covers the arrival window; upstream
delivered-byte and drop counters cover the full simulation.

Each seed has three comparisons: adjacent/baseline, shortest/baseline, and
shortest/adjacent. A metric ratio is numerator mode divided by denominator
mode, and its percent change is `100 * (ratio - 1)`. Lower FCT and higher
goodput are favorable. A ratio involving a nonfinite value or a zero
denominator is undefined, including finite/infinite FCT. An overflowing
ratio or derived percentage is also undefined. This avoids interpreting a
censoring sentinel as a measured zero ratio.

Across seeds, the summary is explicitly the **arithmetic mean of paired
ratios**, with equal seed weights. It is neither a ratio of means nor a
pooled-flow quantile. For example, per-seed baseline p99s `(1, 10)` and adjacent
p99s `(2, 10)` give a mean paired ratio of `1.5`; the different ratio of means
is `12/11`. If any seed ratio is undefined, the summary mean and range for
that metric are withheld, with the number of defined/total ratios shown.
Undefined seeds are not silently dropped.

The separate shortest-versus-adjacent table reports:

- incremental small-flow eligibility:
  `small_other_flows / small_flows` in the analyzed arrival window;
- incremental byte eligibility:
  `small_byte_share - adjacent_small_byte_share`, a fraction of **all generated
  bytes**, not merely analyzed small-flow bytes;
- all-small and non-adjacent-small p99 gain relative to adjacent, positive
  when shortest is faster;
- large mean goodput percent change relative to adjacent.

Eligibility describes the population to which the broader policy applies,
not an observed change in route or fabric traffic. With multiple hosts on a
router, `small_other_flows` can include same-router traffic; the aggregate
cannot separate it, so the apparent extra fabric reach is an upper bound.
The flow-share and byte-share denominators also use different windows.

## Pareto and sample limits

The descriptive Pareto frontier compares only the three modes within the
**same scenario, load, and seed**, using lower all-small p99 and higher large
mean goodput. One mode dominates another only if both objectives are at least
as good and one is strictly better. Exact ties remain on the frontier.
Any empty objective population or nonfinite objective in any mode withholds
the whole group's frontier.

These frontiers are exploratory descriptions of observed measurements. They
contain no uncertainty intervals, significance test, or crossover claim.
Changing a cutoff changes which flows are small and which remain large.
Consequently, frontiers are never constructed across cutoff scenarios, and
the report cannot identify a globally optimal cutoff from incomparable
small-class p99s. Comparing cutoffs for a common population would need
additional raw-flow analysis and a clearly chosen objective.

Warnings report whole-run unfinished counts, nonfinite metrics, exclusions,
and class sample sizes with fewer than 100 observations in a nominal 1% tail
(`floor(n/100) < 100`, hence `n < 10000`). This is a sample-size diagnostic,
not the observed number strictly above p99; ties can make those differ.
Per-seed values remain visible even when sample warnings apply. No number of
seeds or tail observations makes this report a significance analysis.

## Outputs and tests

The new output directory contains:

| File | Contents |
| --- | --- |
| `H3.md` | Per-mode measurements, per-seed comparisons, incremental eligibility/gain, ratio summaries, frontiers, and warning examples |
| `h3-runs.csv` | All validated input runs, normalized core values, and inclusion/exclusion status |
| `h3-comparisons.csv` | Per-seed ratios, percent changes, definition status, and incremental values |
| `h3-summary.csv` | Mean of paired ratios, range, defined/total seeds, and status per metric |
| `h3-pareto.csv` | Per-seed mode frontier membership, dominators, and withholding status |
| `h3-exclusions.csv` | Every excluded incomplete group, including wholly absent requested seeds |
| `h3-warnings.csv` | All warnings; Markdown shows the first 20 |
| `h3-provenance.json` | Input/script hashes, source metadata, options, counts, and interpretation |

Run only the lightweight synthetic tests with:

```sh
PYTHONDONTWRITEBYTECODE=1 python3 -m unittest discover \
    -s test/analysis -p test_h3_report.py -v
```

Fixtures are created in temporary directories. Tests exercise pairing,
population and fingerprint mismatches, ratio estimands, incremental shares,
frontier scope and ties, censoring/empty classes/zero denominators, input
stability, CLI behavior, provenance, and output protection. They do not read
real experimental results or run simulations.
