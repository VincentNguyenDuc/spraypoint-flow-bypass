# CSE 561: Shortest-Path Bypass for Small Flows in Spraypoint

This project tests whether routing small flows on shortest paths, instead of spraying them, lowers their tail latency on the fabric from *RNG: Flat Datacenter Networks at Scale*, and what that costs other traffic.

The repository has four programs:

- **Verifier** (`spraypoint-verifier`): checks that the Spraypoint logic is correct, against the paper.
- **Model check** (`bypass-model`): checks the bypass's models on the same routing tables, without simulating packets: path lengths, shortest-path counts, and worst-case throughput as the small flows' share of bytes grows.
- **TCP experiment** (`tcp-experiment`): an ns-3 simulation measuring flow completion times under default Spraypoint.
- **TCP bypass** (`tcp-bypass`): the same simulation with the small-flow bypass on shortest paths between all routers (`--bypass=shortest`). It also supports `--bypass=adjacent`, the direct link between neighboring routers only, which the report sets aside (Section 3) and the experiments no longer run.

**Start with the report, [report/report.pdf](report/report.pdf)**: it explains the project from the beginning, the models, and how to reproduce the results; its appendices cover the simulator's traffic patterns and what each run records, the workload sweeps, and the bypass in a switch pipeline. Rebuild it with `latexmk` in `report/`.

The first CMake configure downloads, builds, and installs the pinned official ns-3 under `third_party/`, which takes several minutes and happens once.

## Structure

The code is built in layers around one idea: the Spraypoint logic is independent of the simulator, and each experiment adds only what makes it different.

```mermaid
flowchart TD
    experiments["experiments/<br/>one script per experiment"] --> bypass["src/tcp-bypass/<br/>small-flow bypass"]
    experiments --> tcp
    tests["test/<br/>verifier and other tests"] --> core
    bypass -->|"extends"| tcp["src/tcp/<br/>TCP experiment"]
    tcp --> core["src/spraypoint/<br/>topology and routing tables"]
    tcp --> ns3["ns-3<br/>links, IP, TCP, events"]
```

- **Spraypoint core (`src/spraypoint/`):** builds the random router graph and the spray and pointing tables. It answers one question, "which neighbor is next?", and knows nothing about packets or ns-3.
- **TCP experiment (`src/tcp/`):** connects the core to ns-3. It builds the network, runs TCP flows over it, and records the results: `traffic-pattern` picks each flow's endpoints (report, Appendix B).
- **TCP bypass (`src/tcp-bypass/`):** the same experiment, except that small flows can skip the spray step. It adds only that routing decision, through the two extension points the TCP experiment offers: `TcpExperiment::CreateRouting` picks each router's routing, and `SpraypointRouting::SelectNextHop` picks a packet's next hop. A new routing variant can follow the same pattern as a separate program.
- **Tests (`test/`):** one folder per test. The verifier compares the core with the paper, without ns-3; `bypass-model` checks the bypass's models on the same tables, and its output is experiment data (`05-bypass-model`).
- **Experiments (`experiments/`):** one script per experiment; see below.
- **Analysis (`experiments/analysis/`):** Python scripts that read one batch of results and write tables and figures: `01-baseline.py`, `02-bypass-comparison.py`, `03-model-check.py` (the flow-level check against the permutation sweep) and `04-run-summary.py` (per-run health checks, paired comparisons and crossover candidates, for any sweep), plus the standalone `bootstrap.py` (confidence intervals from `04-run-summary.py`'s `runs.csv`) and `workload_cdf.py`.
- **Report (`report/`):** the LaTeX report. `experiments/analysis/report_figures.py` reads pinned batches through the same helpers as the analysis scripts and writes its figures and tables.
- `REPRODUCE.sh` runs every step, from building to the analysis; `REMOTE.sh` runs it on a server over SSH; `cmake/Ns3.cmake` fetches and builds ns-3.

### How experiments run

An experiment is a script that says which runs to do. Data moves one way, from scripts to results to analysis, so analysis can be rerun without rerunning simulations.

- **Scripts (`experiments/NN-*.sh`):** each one is a complete, repeatable experiment, numbered in the order to run them. Sweeps that are compared share a `NN-*-settings.sh`, and each has a `-baseline` and a `-bypass` script that run the same flows. To add an experiment, copy one and change its settings.
- **Shared logic (`experiments/common.sh`):** builds the program and runs the simulations, `JOBS` at a time. It skips finished runs, so an interrupted experiment continues where it stopped.
- **Results (`experiments/results/<machine>/<batch>/<script name>/<run name>/`):** each run's data, with the exact options, commit and machine that produced it. Git ignores them. `<machine>` is `local`, or `remote` for runs from `REMOTE.sh`. A batch is named `<date>_<time>_<code commit>`, where the code commit is the last one to change `src/`, `cmake/`, `CMakeLists.txt`, the numbered experiment scripts, or `test/spraypoint-verifier/` and `test/bypass-model/`. Scripts keep adding to the newest batch for the same code, so a code change starts a new batch and old results never mix with new ones; `NEW_BATCH=1` starts one anyway (`experiments/batch.sh`).
- **Comparing runs:** the same seed and settings give every program the same topology and flows, so differences come from the routing. Compare runs from one batch only: the same seed can give different flows on Mac and Linux, and different code can give different results.

## Experiments

Keep this table in step with the scripts in `experiments/`, which are the source of truth. Each simulated experiment has a `-baseline` script (no bypass) and a `-bypass` script (shortest-path bypass) that run the same flows; most also share a `-settings` file.

Unless a note says otherwise, a run has 256 routers of degree 16 with 4 hosts each, 4 waypoints and 2 pointing next hops; 100 Gbps links with 500 ns delay; router queues of 100 packets and host queues of 1,000; flows arriving for 4 ms with a 1 ms release delay, 90% of them 10 KB and 10% 1 MB, so small flows carry f = 8.3% of the bytes; and a 1 ms minimum TCP retransmission timeout.

| Experiment | What it tests | Traffic | What varies | Seeds | Notes |
| --- | --- | --- | --- | --- | --- |
| `01-smoke` | the whole pipeline end to end, in seconds | uniform | load 0.1 and 0.3; bypass at 0.3 | 561 | 64 routers of degree 8 with 2 hosts each; flows for 1 ms, release delay 500 µs |
| `02-small-sweep` | a quick sweep on a small network | uniform | load 0.1–0.7 | 561–565 | 64 routers of degree 8 with 2 hosts each |
| `03-load-sweep` | H1, H3, and H2 with the default workload | uniform | load 0.1–0.9 | 561–563 | |
| `04-oversubscription` | H2 with a busier fabric at the same host load | uniform | 6 or 8 hosts per router, load 0.5 | 561–563 | |
| `05-bypass-model` | the models of Section 5, at flow level without packets | permutation | small byte share f 0–30%, at 256 and 1,000 routers | 561–563 | not a simulation: runs `bypass-model` on the routing tables, at degree 16 and 64 |
| `06-permutation` | H2 in the worst case, and the models against ns-3 | router permutation | f 8.3, 20, 30%; load 0.1–0.4 | 561–563 | f set by 10, 27.8 or 47.6 KB small flows; every router sends to one partner |
| `07-share-sweep` | H2 with more bytes in small flows | uniform | f 8.3, 20, 30%; load 0.5, 0.7 | 561–563 | the same small-flow sizes as `06` |
| `08-small-fraction` | H2 with more of the flows small | uniform | 95% or 99% small flows (f 16%, 50%); load 0.3–0.7 | 561–563 | 99% runs at load 0.3 only: about 10 GB per run there, more at higher loads |
| `09-small-size` | H2 with larger small flows | uniform | 50 or 100 KB small flows (f 31%, 47%); load 0.3–0.7 | 561–563 | a flow counts as small up to its small size |
| `10-websearch` | H2 on a published workload | uniform | small-flow cutoff 10 KB, 100 KB, 1 MB; load 0.3–0.7 | 561–563 | WebSearch flow sizes (report, Appendix C) |
| `11-hadoop` | H2 on a published workload | uniform | the same cutoffs; load 0.3–0.7 | 561–563 | Hadoop flow sizes (report, Appendix C) |

Results are not committed. The report's figures and tables come from `experiments/analysis/report_figures.py`, which reads the batches pinned in it; none are pinned yet, so the report has no results until the sweeps are run and pinned.

## Workflow

`./REPRODUCE.sh` runs everything below in order, from building to the analysis. The individual steps are:

```bash
cmake -B build                                 # configure
cmake --build build --parallel                 # compile, into build/bin/
ctest --test-dir build -L quick                # fast tests, in seconds
ctest --test-dir build                         # all tests, including the full verifier
experiments/01-smoke-baseline.sh               # quick end-to-end checks, about 10 seconds each
experiments/01-smoke-bypass.sh
JOBS=6 experiments/02-small-sweep-baseline.sh  # small network; JOBS sets how many runs go at once
JOBS=6 experiments/02-small-sweep-bypass.sh    # same flows as the baseline sweep, with each bypass
JOBS=6 experiments/03-load-sweep-baseline.sh   # larger network, using the same pattern
JOBS=6 experiments/03-load-sweep-bypass.sh
JOBS=3 experiments/04-oversubscription-baseline.sh  # busier fabric, same host load
JOBS=3 experiments/04-oversubscription-bypass.sh
JOBS=1 experiments/05-bypass-model.sh           # flow-level model check; each run uses every core
JOBS=6 experiments/06-permutation-baseline.sh  # permutation traffic around saturation
JOBS=6 experiments/06-permutation-bypass.sh
JOBS=4 experiments/07-share-sweep-baseline.sh  # small flows' share of bytes, uniform traffic
JOBS=4 experiments/07-share-sweep-bypass.sh
JOBS=2 experiments/08-small-fraction-baseline.sh  # more small flows; up to 10 GB per run
JOBS=2 experiments/08-small-fraction-bypass.sh
JOBS=4 experiments/09-small-size-baseline.sh   # larger small flows
JOBS=4 experiments/09-small-size-bypass.sh
JOBS=6 experiments/10-websearch-baseline.sh    # published flow-size distributions
JOBS=6 experiments/10-websearch-bypass.sh
JOBS=4 experiments/11-hadoop-baseline.sh
JOBS=4 experiments/11-hadoop-bypass.sh
cmake --build build --target format            # format the C++ code before committing
```

- **Reproduce an experiment:** run its script. It builds what it needs, and rerunning skips finished runs, so an interrupted experiment picks up where it stopped.
- **Analyze results:** run `python3 -m venv experiments/analysis/.venv && experiments/analysis/.venv/bin/pip install -r experiments/analysis/requirements.txt` once, then run a script with that Python: `01-baseline.py` summarizes a sweep's baseline, `02-bypass-comparison.py` compares its bypass runs with the baseline on the same flows, `03-model-check.py` compares `06-permutation` with the flow-level check in `05-bypass-model`, and `04-run-summary.py` checks every run's health and lists each condition's paired comparison and crossover candidates. Each reads whatever runs have finished in the newest batch with its sweep, `03-load-sweep` unless `--sweep` picks another. Tables and figures group runs by what the sweep varies: the load; hosts per router for `04-oversubscription`; the small byte share and load for `06` to `09`; the cutoff and load for `10` and `11`. `--results` picks another batch folder (or set `RESULTS_MACHINE` and `RESULTS_BATCH`). Each writes `README.md` with its tables, plus one CSV per table and one PNG per figure, to `<batch>/analysis/<sweep>-<name>/`, or to `--output`.
- **Run on a server:** copy `.env.example` to `.env` and fill in the login, then run `./REMOTE.sh start`. It sends the current commit and runs `./REPRODUCE.sh` there, which keeps going after you disconnect. `./REMOTE.sh status` or `logs` shows its progress, and `./REMOTE.sh fetch` copies the results into `experiments/results/remote/`. The server needs `git`, `cmake`, `make`, a C++ compiler, and `python3` with `venv`, and password logins need `sshpass` locally (`brew install sshpass`).
- **Add an experiment:** copy a script in `experiments/` and change its settings.
- **Add a test:** create `test/<name>/` with a `CMakeLists.txt`, and add it to `test/CMakeLists.txt`.
- **Run with other settings:** pass options to a program in `build/bin/`. `--PrintHelp` lists the options of `tcp-experiment` and `tcp-bypass`, and `--help` lists those of `spraypoint-verifier` and `bypass-model`.

The simulator supports `uniform`, `permutation` (cyclic host-ID shift), `router-permutation` (seeded router partners), `hotspot`, and `incast` traffic. Each run's `flows.csv` records every flow's endpoints, size, start and finish times and data retransmissions, and `links.csv` records each port's load and drops. The report's Appendix B describes both.

## To do

- **Run and pin the results.** Run the sweeps (`./REPRODUCE.sh`, or `./REMOTE.sh start` on the server), pin each sweep's batch in `experiments/analysis/report_figures.py`, and fill in the report's results.
- **Count timeouts exactly.** Timeouts are now inferred from completion time and `data_retransmissions` (`experiments/analysis/common.py`). Per-flow TCP counters of data retransmission timer firings and SYN/SYN-ACK retries would make them exact and tell a timeout from a fast retransmit. Yusong implemented these in commit `4c6baaa` (`src/tcp/tcp-diagnostics.{h,cc}`, removed in `453fe48` so new runs match the existing batches); restoring them means rerunning the sweeps.
