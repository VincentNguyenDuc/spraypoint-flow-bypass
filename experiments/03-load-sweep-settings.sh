# Settings shared by the load sweeps, so that their results can be compared.
#
# The network keeps the full-scale design's proportions (n=1000, d=64): the same
# share of adjacent router pairs and of routers at each pointing level.  With 4
# hosts per router, fabric utilization is roughly equal to the load.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000)
LOADS=(0.1 0.3 0.5)
SEEDS=(561 562 563)
