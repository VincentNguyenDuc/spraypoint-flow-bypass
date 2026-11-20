"""Command-line options shared by the analysis scripts.

common.py picks its sweep and batch from environment variables when it is imported, so
parse() sets them from the options first; import common after calling it."""

import argparse
import os
from pathlib import Path

import matplotlib

# Scripts only write image files, so no display is needed.
matplotlib.use("Agg")


def parse(description):
    parser = argparse.ArgumentParser(description=description)
    parser.add_argument("--sweep", help="sweep to read, such as 06-permutation (SWEEP; default 03-load-sweep)")
    parser.add_argument("--results", type=Path,
                        help="batch folder to read (RESULTS_DIR; default the newest batch with the sweep)")
    parser.add_argument("--output", type=Path,
                        help="folder to write (ANALYSIS_DIR; default <batch>/analysis/<sweep>-<script>/)")
    args = parser.parse_args()
    if args.sweep:
        os.environ["SWEEP"] = args.sweep
    if args.results:
        os.environ["RESULTS_DIR"] = str(args.results)
    if args.output:
        os.environ["ANALYSIS_DIR"] = str(args.output)
    return args
