# Settings shared by the WebSearch sweeps, so that their results can be compared.
#
# Flow sizes drawn from the web search workload from DCTCP (experiments/workloads/websearch.cdf), with
# small-flow cutoffs of 10 KB, 100 KB and 1 MB, which put about 0.04%, 1.0% and 5.0% of the bytes
# in small flows.  Uniform traffic on the 03-load-sweep network; each run needs under 2 GB.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000 --flowSizeCdf=experiments/workloads/websearch.cdf)
CUTOFFS=(10000 100000 1000000)
LOADS=(0.3 0.5 0.7)
SEEDS=(561 562 563)
