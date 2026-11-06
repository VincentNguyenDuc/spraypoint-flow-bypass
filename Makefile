.PHONY: reproduce reproduce-smoke verify format

CPP_SOURCES := $(wildcard simulator/*.cc simulator/*.h verification/*.cc verification/*.h)
CXX ?= c++
VERIFY_SOURCES := $(wildcard verification/*.cc) simulator/spraypoint.cc simulator/max-flow.cc simulator/flow-size-cdf.cc
VERIFY_HEADERS := $(wildcard verification/*.h) simulator/spraypoint.h simulator/max-flow.h simulator/hash.h simulator/flow-size-cdf.h

# The runner downloads the pinned ns-3 source on first use and writes a CSV.
reproduce:
	python3 scripts/run_rng_experiments.py

# Fast end-to-end check for a new clone or CI environment.
reproduce-smoke:
	python3 scripts/run_rng_experiments.py --seeds 561 --flows 1000 --samples 20 --output results/rng_smoke.csv

# Check the routing implementation against the paper's §5 rules and §7/§9
# numbers. Builds without ns-3; pass options with ARGS="--matchings=100".
verify: build/verify-rng
	./build/verify-rng $(ARGS)

build/verify-rng: $(VERIFY_SOURCES) $(VERIFY_HEADERS)
	mkdir -p build
	$(CXX) -std=c++23 -O2 -Wall -Wextra -pthread -Isimulator $(VERIFY_SOURCES) -o $@

# Apply ns-3's C++ style from .clang-format (needs clang-format 19 or later).
format:
	clang-format -i $(CPP_SOURCES)
