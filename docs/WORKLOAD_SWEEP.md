# Workload sweep (H2)

The workload sweep, `08-workloads`, tests H2 on workloads that put more of the bytes in
small flows than the default 90% of 10 KB flows. It runs on the `03-load-sweep` network
(256 routers of degree 16, 4 hosts each), at loads 0.3, 0.5 and 0.7, seeds 561–563, with
no bypass and with the shortest-path bypass. Each scenario is compared with its own
baseline on the same flows. The default workload itself is `03-load-sweep`, which the
sweep runs alongside at loads 0.1–0.9.

| Scenario | Workload |
| --- | --- |
| `fraction95`, `fraction99` | 10 KB small flows, 95% or 99% of flows; 1 MB large flows |
| `size50k`, `size100k` | 50 KB or 100 KB small flows, 90% of flows; 1 MB large flows |
| `web10k`, `web100k`, `web1m` | the WebSearch flow-size CDF, small-flow cutoff 10 KB, 100 KB or 1 MB |
| `hadoop10k`, `hadoop100k`, `hadoop1m` | the Hadoop flow-size CDF, with the same cutoffs |

The scenarios are defined in `experiments/08-workloads-settings.sh`. The CDF sampler
interpolates linearly between points. A cutoff changes which flows count as small, so
compare modes within a scenario, and take care comparing classes across cutoffs.
[WORKLOAD_CDF.md](WORKLOAD_CDF.md) gives each cutoff's expected share of flows and bytes.

## Running it

Large CDF scenarios need far more memory than the default workload, so the sweep runs
through its own queue rather than `JOBS` alone:

```bash
RESULTS_BATCH=<unique-batch-name> JOBS=8 bash experiments/run-workloads.sh --memory-gb 12
```

`run-workloads.sh` validates the simulator, freezes the sources, programs and ns-3
libraries into the batch, and starts `experiments/workload-queue.py`. The queue plans
210 runs (30 for the default workload and 180 for the scenarios) from the experiment
scripts and runs them from one global queue, so different scenarios, seeds and modes
share the available slots. As runs finish, it reruns
`experiments/analysis/04-workloads.py` on the batch. `./REPRODUCE.sh` runs the same
command when `RUN_WORKLOADS=1` is set.

- **Concurrency:** `JOBS` (or `--jobs`) caps simultaneous runs. Without it, the queue uses
  the CPUs allowed by affinity and cgroup quota. Memory admission can lower the actual
  concurrency. A running queue keeps the limit it started with; resume the batch with a
  new limit to change it.
- **Memory:** `--memory-gb` sets the total reservation budget in GiB, and `--reserve-gb`
  (default 2) leaves memory for other processes. Without `--memory-gb`, the budget is the
  memory available at startup minus the reserve. The queue checks free memory and the
  workers' live RSS before admitting a run, and backfills from other scenarios when a
  large run doesn't fit.
- **Deferred runs:** each run's reservation is a conservative estimate from the default
  workload's peaks and the flows generated per byte, not a measured peak or a hard limit.
  At 12 GiB, 24 runs are deferred: all `fraction99` runs and `fraction95` at load 0.7. They
  stay pending for a larger machine while the rest finish. If only deferred runs remain,
  the status becomes `blocked-memory` with exit status 2, never `complete`.
- **Resuming:** rerun with the same `RESULTS_BATCH` to continue with its frozen simulator
  and settings. Finished runs are checked against their exact options and skipped;
  interrupted or failed attempts move to `attempts/`. A per-batch lock stops two queues
  from writing the same batch. The plan, queue revision and resource settings are saved
  under `scheduler/`.
- **Previewing:** add `--dry-run` to print the plan and the deferred runs without
  building or running anything.
- **Status:** `scheduler-progress.json` and the periodic `PROGRESS` log lines show
  counts, active runs, memory reservations and deferred work. `status` is `running`,
  `complete`, `failed`, `stopped` or `blocked-memory`, and `exit-status` is respectively
  absent, 0, 1, 130 or 2. Ctrl-C, SIGTERM and SIGHUP stop the queue and every worker,
  keeping their partial output. `manifest.txt`, the scheduler files and each run's logs
  identify the exact inputs. The analysis runs in a separate process, logging to
  `analysis/report.log`.

To run part of the sweep with the ordinary scripts instead, without memory admission,
select it with `EXPERIMENT_SCENARIOS`, `EXPERIMENT_LOADS` and `EXPERIMENT_SEEDS`, and use
one `RESULTS_BATCH` for both modes:

```bash
export RESULTS_BATCH=workloads-web100k EXPERIMENT_SCENARIOS=web100k
JOBS=6 experiments/08-workloads-baseline.sh
JOBS=6 experiments/08-workloads-bypass.sh
```

The batch must belong to the current code and machine: with uncommitted code, separate
invocations otherwise start separate batches. Don't reuse a batch after changing the
simulator or the experiment settings.

## Analysis

`experiments/analysis/04-workloads.py --results <batch>` writes
`<batch>/analysis/08-workloads-summary/`:

- `README.md`: the paired comparisons, crossover status and bypass traffic volume;
- `runs.csv`: one row per run, with per-class p50/p99 FCT, measured timeout and data
  retransmission shares, large-flow goodput, fan-in, busiest-link utilization, queue
  occupancy and drops by port kind;
- `comparisons.csv`: each bypass run against its baseline on the same flows;
- `pending.csv`: runs not finished yet.

It uses raw FCTs, the measured TCP counters ([TCP_DIAGNOSTICS.md](TCP_DIAGNOSTICS.md)),
per-port drop reconciliation, and matched seeds and flows. It reports exploratory
crossover candidates and flags incomplete coverage. `01-baseline.py` and
`02-bypass-comparison.py` also accept `--sweep 08-workloads`, with one panel per scenario.

For paired confidence intervals on p99 by topology seed, run
`experiments/analysis/bootstrap.py` on a stable copy of `runs.csv` or on the finished
runs, writing to a new directory. [STATISTICS.md](STATISTICS.md) defines its estimands,
pairing checks, censoring and few-seed limits. Three seeds don't establish a crossover.
