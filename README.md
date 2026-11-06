# CSE 561: Shortest-Path Bypass for Small Flows in Spraypoint

This project tests whether routing small flows on shortest paths, instead of spraying them, lowers their tail latency on the fabric from *RNG: Flat Datacenter Networks at Scale*, and what that costs other traffic. See the [proposal](PROJECT_PROPOSAL.md).

The repository has two programs:

- **Verifier:** checks that the Spraypoint logic is correct, against the paper.
- **TCP experiment:** an ns-3 simulation comparing flow completion times under default Spraypoint and the small-flow bypass.

The first CMake configure downloads, builds, and installs the pinned official ns-3 under `third_party/`, which takes several minutes and happens once.

## Structure

- `src/spraypoint/`: core topology and routing tables, shared by both programs
- `src/tcp/`: the TCP experiment
- `test/`: one folder per test, including the verifier
- `experiments/`: one script per experiment, writing to `results/<script name>/`
- `cmake/Ns3.cmake`: fetches and builds ns-3

## Workflow

```bash
cmake -B build                                 # configure; add -DBUILD_TCP_EXPERIMENT=OFF to skip ns-3
cmake --build build --parallel                 # compile, into build/bin/
ctest --test-dir build -L quick                # fast tests, in seconds
ctest --test-dir build                         # all tests, including the full verifier
experiments/tcp-baseline-smoke.sh              # quick end-to-end check, about 10 seconds
JOBS=8 experiments/tcp-baseline-load-sweep.sh  # full experiment; JOBS sets how many runs go at once
cmake --build build --target format            # format the C++ code before committing
```

- **Reproduce an experiment:** run its script. It builds what it needs, and rerunning skips finished runs, so an interrupted experiment picks up where it stopped.
- **Add an experiment:** copy a script in `experiments/` and change its settings.
- **Add a test:** create `test/<name>/` with a `CMakeLists.txt`, and add it to `test/CMakeLists.txt`.
- **Run with other settings:** pass options to `build/bin/tcp-experiment` or `build/bin/spraypoint-verifier`. `--PrintHelp` lists the TCP experiment's options, and `--help` lists the verifier's.
