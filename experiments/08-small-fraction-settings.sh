# Settings shared by the small-fraction sweeps, so that their results can be compared.
#
# Raises the small flows' share of bytes f by making more of the flows small: 95% or 99%
# of flows at 10 KB, the rest at 1 MB, which gives f = 16% and 50%.  Uniform traffic on
# the 03-load-sweep network.  With 99% small flows a run generates about ten times as
# many flows as the default workload and needs about 10 GB at load 0.3, and roughly 16
# and 24 GB at loads 0.5 and 0.7, so 99% runs only at load 0.3: 09-small-size reaches
# f = 47% at every load with far fewer flows.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000 --smallBytes=10000 --smallThreshold=10000)
FRACTIONS=(0.95 0.99)
LOADS=(0.3 0.5 0.7)
SEEDS=(561 562 563)

# loads_for <fraction>: the loads to run with that fraction of small flows.
loads_for() {
    if [[ $1 == 0.99 ]]; then
        echo 0.3
    else
        echo "${LOADS[@]}"
    fi
}
