# CSE 561: Shortest-Path Bypass for Small Flows in Spraypoint

This project tests whether routing small flows on shortest paths, instead of spraying them, lowers their tail latency on the fabric from *RNG: Flat Datacenter Networks at Scale*, and what that costs other traffic. See the [proposal](PROJECT_PROPOSAL.md).

The repository has three programs:

- **Verifier:** checks that the Spraypoint logic is correct, against the paper.
- **TCP experiment:** an ns-3 simulation measuring flow completion times under default Spraypoint.
- **TCP bypass:** the same simulation with a small-flow bypass, on the direct link between neighboring routers (`--bypass=adjacent`) or on shortest paths between all routers (`--bypass=shortest`).

The first CMake configure downloads, builds, and installs the pinned official ns-3 under `third_party/`, which takes several minutes and happens once.

## Structure

See [ARCHITECTURE.md](ARCHITECTURE.md) for how these fit together.

- `src/spraypoint/`: core topology and routing tables, shared by all programs
- `src/tcp/`: the TCP experiment
- `src/tcp-bypass/`: the TCP bypass, built on `src/tcp/`
- `test/`: one folder per test, including the verifier
- `experiments/`: one script per experiment, writing to `experiments/results/<machine>/<batch>/<script name>/`
- `experiments/analysis/`: notebooks that read one batch of `experiments/results/`
- [ANALYSIS.md](ANALYSIS.md): what the results show so far
- `REPRODUCE.sh`: runs every step, from building to the analysis
- `REMOTE.sh`: runs `REPRODUCE.sh` on a server over SSH
- `cmake/Ns3.cmake`: fetches and builds ns-3

## Experiments

Keep this table in step with the scripts in `experiments/`, which are the source of truth.

| Experiment | Routers | Degree | Hosts per router | Span | Loads | Seeds | Bypass modes | Release delay |
| --- | ---: | ---: | ---: | ---: | --- | --- | --- | ---: |
| `01-smoke` | 64 | 8 | 2 | 1 ms | 0.1, 0.3 with no bypass; 0.3 with a bypass | 561 | none, adjacent, shortest | 500 µs |
| `02-small-sweep` | 64 | 8 | 2 | 4 ms | 0.1, 0.3, 0.5, 0.7 | 561–565 | none, adjacent, shortest | 1 ms |
| `03-load-sweep` | 256 | 16 | 4 | 4 ms | 0.1, 0.3, 0.5 | 561–563 | none, adjacent, shortest | 1 ms |
| `04-oversubscription` | 256 | 16 | 6, 8 | 4 ms | 0.5 | 561–563 | none, adjacent, shortest | 1 ms |

All experiments share these settings:

- **Routing:** 4 waypoints and 2 pointing next hops
- **Links:** 100 Gbps with 500 ns delay
- **Queues:** 100 packets at routers and 1,000 at hosts
- **Traffic:** uniform random hosts, with 90% of flows at 10 KB and 10% at 1 MB
- **TCP:** 1 ms minimum retransmission timeout

## Workflow

`./REPRODUCE.sh` runs everything below in order, from building to the analysis notebooks. The individual steps are:

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
cmake --build build --target format            # format the C++ code before committing
```

- **Reproduce an experiment:** run its script. It builds what it needs, and rerunning skips finished runs, so an interrupted experiment picks up where it stopped.
- **Analyze results:** run `python3 -m venv experiments/analysis/.venv && experiments/analysis/.venv/bin/pip install -r experiments/analysis/requirements.txt` once, then `experiments/analysis/.venv/bin/jupyter lab experiments/analysis/`. The notebooks use whatever runs have finished in the newest batch with the sweep they analyze, `03-load-sweep` unless `SWEEP` picks another; set `RESULTS_MACHINE` and `RESULTS_BATCH` to pick another batch.
- **Run on a server:** copy `.env.example` to `.env` and fill in the login, then run `./REMOTE.sh start`. It sends the current commit and runs `./REPRODUCE.sh` there, which keeps going after you disconnect. `./REMOTE.sh status` or `logs` shows its progress, and `./REMOTE.sh fetch` copies the results into `experiments/results/remote/`. The server needs `git`, `cmake`, `make`, a C++ compiler, and `python3` with `venv`, and password logins need `sshpass` locally (`brew install sshpass`).
- **Add an experiment:** copy a script in `experiments/` and change its settings.
- **Add a test:** create `test/<name>/` with a `CMakeLists.txt`, and add it to `test/CMakeLists.txt`.
- **Run with other settings:** pass options to a program in `build/bin/`. `--PrintHelp` lists the options of `tcp-experiment` and `tcp-bypass`, and `--help` lists those of `spraypoint-verifier`.
