.PHONY: reproduce reproduce-smoke

# The runner downloads the pinned ns-3 source on first use and writes a CSV.
reproduce:
	python3 scripts/run_rng_experiments.py

# Fast end-to-end check for a new clone or CI environment.
reproduce-smoke:
	python3 scripts/run_rng_experiments.py --seeds 561 --flows 1000 --samples 20 --output results/rng_smoke.csv
