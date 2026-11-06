# Settings shared by the small sweeps, so that their results can be compared.
#
# A network small enough to sweep in minutes.  With 2 hosts per router and degree 8,
# fabric utilization is roughly 0.8 times the load.  Its proportions differ from the
# full-scale design (n=1000, d=64): about 13% of router pairs are adjacent, not 6%.
NETWORK=(--nodes=64 --degree=8 --waypoints=4 --nextHops=2 --hostsPerRouter=2)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000)
LOADS=(0.1 0.3 0.5 0.7)
SEEDS=(561 562 563 564 565)
