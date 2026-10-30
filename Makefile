.PHONY: reproduce reproduce-smoke format

CPP_SOURCES := $(wildcard simulator/*.cc simulator/*.h)

# The runner downloads the pinned ns-3 source on first use and writes a CSV.
reproduce:
	python3 scripts/run_rng_experiments.py

# Fast end-to-end check for a new clone or CI environment.
reproduce-smoke:
	python3 scripts/run_rng_experiments.py --seeds 561 --flows 1000 --samples 20 --output results/rng_smoke.csv

# Apply ns-3's C++ style from .clang-format (needs clang-format 19 or later).
format:
	clang-format -i $(CPP_SOURCES)
