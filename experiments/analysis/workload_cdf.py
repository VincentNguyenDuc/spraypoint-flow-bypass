#!/usr/bin/env python3
"""Compute flow and byte coverage using the simulator's rounded CDF sampler."""
import argparse
import csv
from dataclasses import dataclass
import math
from pathlib import Path
import sys


def sampled_size(value):
    return max(1, math.floor(value + .5))


def size_integral(value):
    """Integral from zero to value of max(1, floor(x + .5)), for x >= 0."""
    n = math.floor(value + .5)
    return n * (n - 1) / 2 + n * (value + .5 - n) + min(value, .5)


def segment_mean(start, end):
    """Mean rounded/clamped size for uniform X on a nonempty [start, end].

    Center on the first rounded bin instead of subtracting large antiderivatives.
    Interior bins have width one, and only the last bin contributes a partial
    width above that baseline. This also handles the wider clamped one-byte bin.
    """
    first, last = sampled_size(start), sampled_size(end)
    if first == last:
        return float(first)
    interior_bins = last - first - 1
    interior_excess = interior_bins * (interior_bins + 1) // 2
    width = end - start
    last_fraction = (end - (last - .5)) / width
    return math.fsum((first, interior_excess / width, (last - first) * last_fraction))


@dataclass(frozen=True)
class WorkloadCdf:
    points: tuple

    def __post_init__(self):
        previous = (0, 0)
        for size, probability in self.points:
            if (not math.isfinite(size) or not math.isfinite(probability)
                    or not 0 <= size <= 2 ** 32 - 1 or not 0 <= probability <= 1
                    or size < previous[0] or probability < previous[1]):
                raise ValueError("CDF sizes/probabilities must be finite, ascending and within range")
            previous = size, probability
        if not self.points or self.points[-1][1] != 1:
            raise ValueError("CDF must end at probability 1")

    @classmethod
    def read(cls, path):
        points = []
        for line in path.read_text().splitlines():
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = line.split()
            if len(fields) != 2:
                raise ValueError(f"Expected bytes and cumulative probability: {line}")
            points.append(tuple(map(float, fields)))
        return cls(tuple(points))

    def mean_for_arrival_rate(self):
        # ns-3 FlowSizeCdf::Mean uses the continuous interpolation, before rounding.
        return self.points[0][0] * self.points[0][1] + sum(
            (pb - pa) * (a + b) / 2
            for (a, pa), (b, pb) in zip(self.points, self.points[1:]))

    def moments(self, cutoff=None):
        """Probability and expected bytes below cutoff; None includes every flow."""
        if cutoff is not None and (not isinstance(cutoff, int) or cutoff < 0):
            raise ValueError("Cutoff must be a nonnegative integer byte count")
        first, mass = self.points[0]
        allowed = cutoff is None or sampled_size(first) <= cutoff
        probability, byte_moment = (mass, mass * sampled_size(first)) if allowed else (0, 0)
        if cutoff == 0:
            return 0.0, 0.0
        boundary = float("inf") if cutoff is None else cutoff + .5
        for (a, pa), (b, pb) in zip(self.points, self.points[1:]):
            mass = pb - pa
            if mass == 0:
                continue
            if a == b:
                if cutoff is None or sampled_size(a) <= cutoff:
                    probability += mass
                    byte_moment += mass * sampled_size(a)
                continue
            end = min(b, boundary)
            if end <= a:
                continue
            # Form a bounded fraction before multiplying by mass: a density can
            # overflow for subnormal widths even when the probability is finite.
            retained_mass = mass * ((end - a) / (b - a))
            probability += retained_mass
            byte_moment += retained_mass * segment_mean(a, end)
        return probability, byte_moment

    def coverage(self, cutoff):
        probability, byte_moment = self.moments(cutoff)
        _, mean_sampled = self.moments()
        return dict(threshold_bytes=cutoff, flow_fraction=probability,
                    byte_fraction=byte_moment / mean_sampled,
                    eligible_expected_bytes=byte_moment,
                    eligible_mean_bytes=byte_moment / probability if probability else None,
                    mean_sampled_bytes=mean_sampled,
                    mean_arrival_bytes=self.mean_for_arrival_rate())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cdf", type=Path, action="append", required=True)
    parser.add_argument("--threshold", type=int, action="append", default=[])
    args = parser.parse_args(argv)
    try:
        rows = [dict(workload=str(path), **WorkloadCdf.read(path).coverage(cutoff))
                for path in args.cdf for cutoff in (args.threshold or [10000, 100000, 1000000])]
    except (ValueError, OSError) as error:
        parser.error(str(error))
    writer = csv.DictWriter(sys.stdout, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)


if __name__ == "__main__":
    main()
