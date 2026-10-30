# CSE 561: Shortest-Path Bypass for Small Flows in Spraypoint

This repository contains an ns-3 reproduction of the RNG/Spraypoint routing baseline from *RNG: Flat Datacenter Networks at Scale*. The simulator source and experiment harness are versioned in this repository; the pinned official ns-3 dependency is downloaded automatically on first run.

## Reproduce the baseline

```bash
make reproduce-smoke
```

The command downloads the pinned ns-3 source, builds the minimal required modules, runs one deterministic seed, and writes `results/rng_smoke.csv`.

For the five-seed baseline sweep, run:

```bash
make reproduce
```

See [REPRODUCING_RNG.md](REPRODUCING_RNG.md) for prerequisites, expected results, technical details, and command-line options.

## Formatting

C++ sources follow ns-3's style, defined in [.clang-format](.clang-format). Run `make format` before committing; it needs clang-format 19 or later.
