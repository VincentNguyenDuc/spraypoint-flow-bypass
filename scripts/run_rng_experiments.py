#!/usr/bin/env python3
"""Run repeatable ns-3 RNG/Spraypoint baseline experiments and save a CSV."""

from __future__ import annotations

import argparse
import csv
import os
import re
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_NS3_DIR = ROOT / "third_party" / "ns-3-dev"
SIMULATOR_DIR = ROOT / "simulator"
SIMULATOR_MAIN = SIMULATOR_DIR / "main.cc"
# ns-3 builds every .cc in a scratch/ subdirectory into one program named
# scratch/<subdirectory>/<file containing main>.
SCRATCH_NAME = "rng-spraypoint"
SCRATCH_PROGRAM = f"scratch/{SCRATCH_NAME}/{SIMULATOR_MAIN.stem}"
NS3_REPOSITORY = "https://gitlab.com/nsnam/ns-3-dev.git"
# Pinned after verifying the baseline reproduction; do not silently build a
# later ns-3 tip when a collaborator runs the experiment months from now.
NS3_REVISION = "c3b0d18fc6d6e61b0cc792eb45d8fe7a1db78d08"
NS3_MODULES = "core;network;point-to-point;internet;applications"


def command(args: list[str], cwd: Path) -> str:
    """Run a command while forwarding its output on failure."""
    completed = subprocess.run(args, cwd=cwd, text=True, capture_output=True)
    if completed.returncode:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        raise RuntimeError(f"command failed ({completed.returncode}): {' '.join(args)}")
    return completed.stdout


def clone_ns3(ns3_dir: Path) -> None:
    """Clone the exact ns-3 revision required by this reproduction."""
    ns3_dir.parent.mkdir(parents=True, exist_ok=True)
    completed = subprocess.run(
        ["git", "clone", NS3_REPOSITORY, str(ns3_dir)], text=True, capture_output=True
    )
    if completed.returncode:
        sys.stderr.write(completed.stdout)
        sys.stderr.write(completed.stderr)
        raise RuntimeError(f"could not clone ns-3 from {NS3_REPOSITORY}")
    command(["git", "checkout", "--detach", NS3_REVISION], ns3_dir)


def ensure_scratch_link(ns3_dir: Path) -> None:
    """Expose the Git-tracked model to ns-3's scratch build directory."""
    # Checkouts made before the simulator was split into several files link
    # the single source file; ns-3 would try to build it as its own program.
    legacy_source = ns3_dir / "scratch" / f"{SCRATCH_NAME}.cc"
    if legacy_source.is_symlink():
        legacy_source.unlink()

    scratch_dir = ns3_dir / "scratch" / SCRATCH_NAME
    relative_target = os.path.relpath(SIMULATOR_DIR, scratch_dir.parent)

    if scratch_dir.is_symlink():
        if scratch_dir.resolve() == SIMULATOR_DIR.resolve():
            return
        scratch_dir.unlink()
    elif scratch_dir.exists():
        raise RuntimeError(
            f"{scratch_dir} exists and is not the managed symbolic link; "
            "move it aside before running this script"
        )
    scratch_dir.symlink_to(relative_target, target_is_directory=True)


def needs_configure(ns3_dir: Path) -> bool:
    """Whether ns-3 is unconfigured, or configured without a module we now need."""
    cache = ns3_dir / "cmake-cache" / "CMakeCache.txt"
    if not cache.is_file():
        return True
    match = re.search(r"^NS3_ENABLED_MODULES:\w+=(.*)$", cache.read_text(), re.MULTILINE)
    enabled = set(match.group(1).split(";")) if match else set()
    return not set(NS3_MODULES.split(";")) <= enabled


def parse_result(output: str, seed: int) -> dict[str, str | int | float]:
    patterns = {
        "parameters": r"parameters n=(\d+) d=(\d+) p=(\d+) h=(\d+) waypoint-levels=(\d+)",
        "delivery": r"delivered=(\d+)/(\d+) hop-limit-drops=(\d+)",
        "paths": r"path_length_fraction L1=([^ ]+) L2=([^ ]+) L3=([^ ]+) L4=([^ ]+) L5=([^\n]+)",
        "latency": r"packet_latency_ns p50=([^ ]+) p99=([^\n]+)",
        "disjoint": r"edge_disjoint_paths sampled=(\d+) adjacent_mean=([^ ]+) nonadjacent_mean=([^\n]+)",
        "model": r"paper_model_nonadjacent=([^ ]+)",
    }
    matches = {name: re.search(pattern, output) for name, pattern in patterns.items()}
    missing = [name for name, match in matches.items() if match is None]
    if missing:
        raise RuntimeError(f"could not parse fields {missing} from ns-3 output:\n{output}")

    n, d, p, h, levels = matches["parameters"].groups()
    delivered, offered, drops = matches["delivery"].groups()
    l1, l2, l3, l4, l5 = matches["paths"].groups()
    p50, p99 = matches["latency"].groups()
    sampled, adjacent, nonadjacent = matches["disjoint"].groups()
    (model,) = matches["model"].groups()
    return {
        "seed": seed,
        "nodes": int(n),
        "degree": int(d),
        "waypoints_p": int(p),
        "next_hops_h": int(h),
        "waypoint_levels": int(levels),
        "offered_flows": int(offered),
        "delivered_flows": int(delivered),
        "hop_limit_drops": int(drops),
        "path_l1": float(l1),
        "path_l2": float(l2),
        "path_l3": float(l3),
        "path_l4": float(l4),
        "path_l5": float(l5),
        "latency_p50_ns": float(p50),
        "latency_p99_ns": float(p99),
        "edge_disjoint_samples": int(sampled),
        "edge_disjoint_adjacent_mean": float(adjacent),
        "edge_disjoint_nonadjacent_mean": float(nonadjacent),
        "paper_model_nonadjacent": float(model),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ns3-dir", type=Path, default=DEFAULT_NS3_DIR)
    parser.add_argument("--seeds", default="561,562,563,564,565", help="comma-separated seeds")
    parser.add_argument("--flows", type=int, default=50_000)
    parser.add_argument("--samples", type=int, default=500)
    parser.add_argument("--output", type=Path, default=ROOT / "results" / "rng_baseline.csv")
    parser.add_argument("--configure", action="store_true", help="force minimal ns-3 reconfiguration")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument(
        "--no-clone-ns3",
        action="store_true",
        help="fail instead of cloning the pinned official ns-3 dependency when it is absent",
    )
    args = parser.parse_args()

    ns3_dir = args.ns3_dir.resolve()
    ns3_program = ns3_dir / "ns3"
    if not ns3_program.is_file():
        if args.no_clone_ns3:
            raise RuntimeError(f"official ns-3 checkout not found at {ns3_dir}")
        print(f"Cloning pinned ns-3 revision {NS3_REVISION[:12]} ...")
        clone_ns3(ns3_dir)
    if not SIMULATOR_MAIN.is_file():
        raise RuntimeError(f"missing Git-tracked simulator source: {SIMULATOR_MAIN}")

    ensure_scratch_link(ns3_dir)
    if args.configure or needs_configure(ns3_dir):
        print("Configuring minimal ns-3 build ...")
        command(
            [
                "./ns3",
                "configure",
                f"--enable-modules={NS3_MODULES}",
                "--disable-examples",
                "--disable-python",
                "--build-profile=release",
            ],
            ns3_dir,
        )
    if not args.skip_build:
        print("Building ns-3 experiment ...")
        command(["./ns3", "build"], ns3_dir)

    seeds = [int(seed.strip()) for seed in args.seeds.split(",") if seed.strip()]
    if not seeds:
        raise RuntimeError("provide at least one seed")
    rows = []
    for seed in seeds:
        print(f"Running seed {seed} ...")
        output = command(
            [
                "./ns3",
                "run",
                f"{SCRATCH_PROGRAM} --flows={args.flows} --samples={args.samples} --seed={seed}",
            ],
            ns3_dir,
        )
        row = parse_result(output, seed)
        rows.append(row)
        print(
            f"  delivered={row['delivered_flows']}/{row['offered_flows']} "
            f"nonadjacent_EDP={row['edge_disjoint_nonadjacent_mean']:.2f}"
        )

    output_path = args.output if args.output.is_absolute() else ROOT / args.output
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(f"Wrote {len(rows)} runs to {output_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
