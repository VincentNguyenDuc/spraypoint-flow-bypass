# Architecture

The code is built in layers around one idea: the Spraypoint logic is independent of the simulator, and each experiment adds only what makes it different.

```mermaid
flowchart TD
    experiments["experiments/<br/>one script per experiment"]
    tests["test/<br/>verifier and other tests"]
    bypass["src/tcp-bypass/<br/>small-flow bypass"]
    tcp["src/tcp/<br/>TCP experiment"]
    core["src/spraypoint/<br/>topology and routing tables"]
    ns3["ns-3<br/>links, IP, TCP, events"]

    experiments --> bypass
    experiments --> tcp
    tests --> core
    tests --> bypass
    bypass -->|"extends"| tcp
    tcp --> core
    tcp --> ns3
```

## The layers

- **Spraypoint core (`src/spraypoint/`):** builds the random router graph and the spray and pointing tables. It answers one question, "which neighbor is next?", and knows nothing about packets or ns-3.
- **TCP experiment (`src/tcp/`):** connects the core to ns-3. It builds the network, runs TCP flows over it, and records the results. Routers use the core's tables to forward real packets.
- **TCP bypass (`src/tcp-bypass/`):** the same experiment, except that small flows can skip the spray step. It adds only that routing decision, so flows, TCP, and measurements stay the same as the baseline.
- **Tests (`test/`):** check the layers from outside. The verifier compares the core with the paper, without ns-3, and other tests check the programs' behavior.
- **Experiments (`experiments/`):** each script fixes the settings for one experiment, runs its program, and writes to `experiments/results/<script name>/`.
- **Analysis (`analysis/`):** notebooks that read `experiments/results/`: one for the baseline, and one comparing it with the bypass.

## Extending the experiment

The TCP experiment has two extension points, and the bypass uses both:

```mermaid
flowchart LR
    be["BypassExperiment"] -->|"extends"| te["TcpExperiment"]
    br["BypassRouting"] -->|"extends"| sr["SpraypointRouting"]
    te -->|"creates each router's"| sr
    be -->|"creates each router's"| br
```

- **`TcpExperiment::CreateRouting`** chooses which routing to put on each router.
- **`SpraypointRouting::SelectNextHop`** chooses a packet's next hop.

A new routing variant can follow the same pattern as a separate program, leaving the existing ones unchanged.

## Comparing experiments

The same seed and settings give every program the same topology and the same flows, so differences between runs come from the routing. To keep that true, sweeps that will be compared share their settings, as the load sweeps do through `experiments/load-sweep-settings.sh`. Compare runs from one platform only, because the same seed can give different flows on Mac and Linux.
