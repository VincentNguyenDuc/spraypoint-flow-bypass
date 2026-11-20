# Settings shared by the share sweeps, so that their results can be compared.
#
# The 03-load-sweep network and uniform traffic, with the small flows' share of bytes,
# f, raised as in 06-permutation: 10 KB small flows give 8.3%, 27,778 B give 20%, and
# 47,619 B give 30%, with 10% of flows at 1 MB.  06-permutation found the bypass's cost
# under worst-case traffic; this checks whether it appears under uniform traffic too,
# at the loads where the fabric is busy.  Only the shortest-path bypass runs: see
# docs/TODO.md.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000)
SMALL_BYTES=(10000 27778 47619)
LOADS=(0.5 0.7)
SEEDS=(561 562 563)
