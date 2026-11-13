# Settings shared by the oversubscription sweeps, so that their results can be compared.
#
# The 03-load-sweep network with more hosts per router, so that the fabric gets busier
# while each host link stays at the same load.  Fabric utilization is roughly
# load x hostsPerRouter x 3.6 hops / 16 links: about 0.68 with 6 hosts and 0.9 with 8
# at load 0.5, against 0.45 with the 4 hosts of 03-load-sweep.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000)
HOSTS_PER_ROUTER=(6 8)
LOADS=(0.5)
SEEDS=(561 562 563)
