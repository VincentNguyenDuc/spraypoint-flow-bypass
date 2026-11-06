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
- `experiments/`: one script per experiment, writing to `results/<script name>/`
- `cmake/Ns3.cmake`: fetches and builds ns-3

## Workflow

```bash
cmake -B build                                 # configure; add -DBUILD_TCP_EXPERIMENT=OFF to skip ns-3
cmake --build build --parallel                 # compile, into build/bin/
ctest --test-dir build -L quick                # fast tests, in seconds
ctest --test-dir build                         # all tests, including the full verifier
experiments/tcp-baseline-smoke.sh              # quick end-to-end checks, about 10 seconds each
experiments/tcp-bypass-smoke.sh
JOBS=8 experiments/tcp-baseline-load-sweep.sh  # full experiments; JOBS sets how many runs go at once
JOBS=8 experiments/tcp-bypass-load-sweep.sh    # same flows as the baseline sweep, with each bypass
cmake --build build --target format            # format the C++ code before committing
```

- **Reproduce an experiment:** run its script. It builds what it needs, and rerunning skips finished runs, so an interrupted experiment picks up where it stopped.
- **Add an experiment:** copy a script in `experiments/` and change its settings.
- **Add a test:** create `test/<name>/` with a `CMakeLists.txt`, and add it to `test/CMakeLists.txt`.
- **Run with other settings:** pass options to a program in `build/bin/`. `--PrintHelp` lists the options of `tcp-experiment` and `tcp-bypass`, and `--help` lists those of `spraypoint-verifier`.
