# Settings shared by the permutation sweeps, so that their results can be compared.
#
# Checks Model 3 of BYPASS_MODELLING.md below and around saturation.  Every router
# sends only to its partner in a random matching, the same matching as matching 0 of
# 05-bypass-model.sh, so the flow-level predictions line up pair by pair.  On the
# 03-load-sweep network, each router offers load x 4 / 16 of its full fabric rate, so
# the busiest ECMP link saturates near load 4 / ecmp_r: about 0.34 with no bypass and
# 0.24-0.30 with a bypassed share f of 20-30% (from 05-bypass-model).
#
# f is the small flows' share of bytes, 0.9 s / (0.9 s + 100 KB) for small flows of
# s bytes and 10% of flows at 1 MB: 10 KB gives 8.3%, 27,778 B gives 20%, and 47,619 B
# gives 30%.  These straddle the model's knees at about 10% (shortest) and 20%
# (adjacent) on this network.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000 --pattern=permutation)
SMALL_BYTES=(10000 27778 47619)
LOADS=(0.1 0.2 0.3 0.4)
SEEDS=(561 562 563)
