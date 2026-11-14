#!/usr/bin/env python3
"""Exercise congested output ports and reconcile their counts with the summary."""
import csv
import pathlib
import re
import subprocess
import sys
import tempfile

with tempfile.TemporaryDirectory() as directory:
    subprocess.run(
        [sys.argv[1], "--nodes=16", "--degree=4", "--hostsPerRouter=2", "--flows=300",
         "--load=4", "--fabricQueuePackets=2", "--nicQueuePackets=20",
         "--smallFraction=1", "--seed=561", f"--outputDir={directory}"],
        check=True, stdout=subprocess.DEVNULL, timeout=120,
    )
    root = pathlib.Path(directory)
    line = next(line for line in (root / "summary.txt").read_text().splitlines()
                if line.startswith("drops "))
    counts = {key: int(value) for key, value in re.findall(r"(\w+)=(\d+)", line)}
    with (root / "links.csv").open(newline="") as file:
        ports = list(csv.DictReader(file))
    kinds = {row["port_kind"] for row in ports}
    assert kinds == {"fabric", "downlink", "host_nic"}, kinds
    total = lambda kind, field: sum(int(row[field]) for row in ports if row["port_kind"] in kind)
    router = total({"fabric", "downlink"}, "queue_drops")
    host = total({"host_nic"}, "queue_drops")
    assert counts["router_queue"] > 0, "test did not create router congestion"
    assert counts["unattributed_router_queue"] == 0, counts
    assert router == counts["router_queue"], (router, counts)
    assert host == counts["host_queue"], (host, counts)
    assert total(kinds, "device_drops") == counts["device"], counts
