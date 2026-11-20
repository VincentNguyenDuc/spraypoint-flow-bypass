# Settings shared by the small-size sweeps, so that their results can be compared.
#
# Raises the small flows' share of bytes f by making the small flows larger: 90% of flows
# at 50 KB or 100 KB, the rest at 1 MB, which gives f = 31% and 47%, past 07-share-sweep's
# 30%.  A flow counts as small up to its small size.  Uniform traffic on the
# 03-load-sweep network.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000 --smallFraction=0.9)
SMALL_BYTES=(50000 100000)
LOADS=(0.3 0.5 0.7)
SEEDS=(561 562 563)
