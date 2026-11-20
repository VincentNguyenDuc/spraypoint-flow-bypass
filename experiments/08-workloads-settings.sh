# Settings shared by the workload sweeps, so that their results can be compared.
#
# Tests H2 on workloads that put more of the bytes in small flows than the default 90%
# of 10 KB flows, on the 03-load-sweep network: more small flows (fraction95,
# fraction99), larger small flows (size50k, size100k), and the published WebSearch and
# Hadoop flow-size distributions with small-flow cutoffs of 10 KB, 100 KB and 1 MB.  The
# default workload itself is 03-load-sweep.  Large CDF scenarios need far more memory
# than the default workload, so run these through experiments/run-workloads.sh, which
# admits runs by their estimated memory; see docs/WORKLOAD_SWEEP.md.
NETWORK=(--nodes=256 --degree=16 --waypoints=4 --nextHops=2 --hostsPerRouter=4)
WORKLOAD=(--flows=0 --spanMs=4 --releaseDelayUs=1000)
SCENARIOS=(fraction95 fraction99 size50k size100k web10k web100k web1m hadoop10k hadoop100k hadoop1m)
LOADS=(0.3 0.5 0.7)
SEEDS=(561 562 563)
# EXPERIMENT_SCENARIOS, EXPERIMENT_LOADS and EXPERIMENT_SEEDS run a subset, such as one
# scenario or one load.
read -r -a SCENARIOS <<< "${EXPERIMENT_SCENARIOS:-${SCENARIOS[*]}}"
read -r -a LOADS <<< "${EXPERIMENT_LOADS:-${LOADS[*]}}"
read -r -a SEEDS <<< "${EXPERIMENT_SEEDS:-${SEEDS[*]}}"

# scenario_options <scenario>: sets SCENARIO_OPTIONS to the workload options of one
# scenario.  Two-size scenarios keep 1 MB large flows.
scenario_options() {
    local cutoff cdf
    case "$1" in
        fraction95) SCENARIO_OPTIONS=(--smallFraction=0.95 --smallBytes=10000 --smallThreshold=10000) ;;
        fraction99) SCENARIO_OPTIONS=(--smallFraction=0.99 --smallBytes=10000 --smallThreshold=10000) ;;
        size50k) SCENARIO_OPTIONS=(--smallFraction=0.9 --smallBytes=50000 --smallThreshold=50000) ;;
        size100k) SCENARIO_OPTIONS=(--smallFraction=0.9 --smallBytes=100000 --smallThreshold=100000) ;;
        web* | hadoop*)
            case "$1" in
                *10k) cutoff=10000 ;;
                *100k) cutoff=100000 ;;
                *1m) cutoff=1000000 ;;
            esac
            case "$1" in
                web*) cdf=websearch ;;
                hadoop*) cdf=hadoop ;;
            esac
            SCENARIO_OPTIONS=("--flowSizeCdf=experiments/workloads/$cdf.cdf" "--smallThreshold=$cutoff")
            ;;
        *)
            echo "Unknown scenario: $1" >&2
            exit 2
            ;;
    esac
}
