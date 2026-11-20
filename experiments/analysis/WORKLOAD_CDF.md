# CDF coverage before running a simulation

`workload_cdf.py` computes how much of a workload a bypass cutoff classifies as
small. It uses only the standard library, reads CDF definitions, and writes CSV
to stdout. It neither opens experimental results nor changes a running batch.

```sh
python3 experiments/analysis/workload_cdf.py \
  --cdf experiments/workloads/websearch.cdf \
  --cdf experiments/workloads/hadoop.cdf \
  --threshold 10000 --threshold 100000 --threshold 1000000
```

For sampled flow size `B` and cutoff `T`, flow coverage is `Pr(B <= T)` and byte
coverage is `E[B * 1(B <= T)] / E[B]`. They have different denominators. A large
fraction of small messages need not account for much traffic volume. These are
population expectations, not measured fractions or evidence of congestion.

The CDF sampler linearly interpolates size as a function of cumulative
probability, rounds positive sizes half-up to integer bytes, and clamps them to
at least one byte. The tool includes initial point masses, repeated-size atoms
and zero-probability plateaus. Integrals are evaluated from rounded size bins
without enumerating byte values or subtracting two large antiderivatives. Narrow
large-valued segments and subnormal widths have regression tests.

`mean_sampled_bytes` includes rounding/clamping, whereas `mean_arrival_bytes`
matches the simulator's continuous `FlowSizeCdf::Mean()` used to set arrival
rate. Their small difference is intentional. The calculation treats the CDF as
a continuous probability distribution and ignores measure-zero exact endpoint
draws and the finite floating-point RNG grid. Inputs must be finite, ascending,
nonnegative sizes within uint32 range, with cumulative probability ending at one.

For the repository workloads, the approximate expected byte coverage is:

| Workload | 10 KB cutoff | 100 KB cutoff | 1 MB cutoff |
| --- | ---: | ---: | ---: |
| WebSearch | 0.044% | 1.02% | 5.04% |
| Hadoop | 0.48% | 7.91% | 31.49% |

Even the 1 MB WebSearch cutoff moves only about 5% of bytes into the small class.
This is context for interpreting H2, not proof that hotspots cannot occur;
destination concentration and actual routed paths matter as well.

Cutoffs change the small/large populations, so their small-class p99s are not
comparable with each other. A cross-cutoff optimization needs a fixed target population and a chosen latency
and throughput objective, plus uncertainty analysis.

```sh
python3 -m unittest discover -s test/analysis -p test_workload_cdf.py -v
```
