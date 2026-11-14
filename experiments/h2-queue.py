#!/usr/bin/env python3
"""Run a frozen H2 matrix through one CPU- and memory-limited process queue."""
import argparse
import dataclasses
import datetime
import fcntl
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time


SCENARIOS = ["fraction95", "fraction99", "size50k", "size100k", "web10k", "web100k",
             "web1m", "hadoop10k", "hadoop100k", "hadoop1m"]
GIB = 1024 ** 3
PLAN_SHIM = r'''
set -euo pipefail
ROOT=${EXPERIMENT_SOURCE_ROOT:?}
EXPERIMENT=${EXPERIMENT:-$(basename "$0" .sh)}
PROGRAM_NAME=${PROGRAM_NAME:-tcp-experiment}
run_experiment() {
    printf '%s\0' "$EXPERIMENT" "$1" "$PROGRAM_NAME" "$(($# - 1))" "${@:2}"
}
finish_runs() { :; }
'''


def atomic_text(path, text):
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(text)
    temporary.replace(path)


def now():
    return datetime.datetime.now().astimezone().isoformat(timespec="seconds")


@dataclasses.dataclass(frozen=True)
class Task:
    experiment: str
    run: str
    program: str
    args: tuple
    memory_gb: float

    @property
    def key(self):
        return f"{self.experiment}/{self.run}"


def memory_estimate(args, source):
    """Conservative admission estimate, not a hard limit or a measured peak.

    Existing default-workload runs use about 8 GiB per unit of offered load.
    Leave headroom and scale up for the number of flows generated per byte.
    Large CDF flows keep the baseline floor because their sockets live longer.
    """
    options = dict(arg[2:].split("=", 1) for arg in args)
    if "flowSizeCdf" in options:
        points = [tuple(map(float, line.split()))
                  for line in (source / options["flowSizeCdf"]).read_text().splitlines()
                  if line.strip() and not line.startswith("#")]
        mean = points[0][0] * points[0][1] + sum(
            (b[1] - a[1]) * (a[0] + b[0]) / 2 for a, b in zip(points, points[1:]))
    else:
        header = (source / "src/tcp/tcp-experiment.h").read_text()
        def option(name):
            return float(options.get(name) or re.search(rf"\b{name}\{{([\d.]+)\}}", header)[1])
        fraction = option("smallFraction")
        mean = fraction * option("smallBytes") + (1 - fraction) * option("largeBytes")
    return max(1.25, 9.5 * float(options["load"]) * max(1, 109000 / mean))


def make_plan(source):
    """Capture the frozen scripts' real options through a planning-only shim.

    The shim never sources common.sh, builds programs, or runs a simulator. This
    also works with snapshots created before the global queue was introduced.
    """
    invocations = [(f"03-load-sweep-{mode}.sh", []) for mode in ["baseline", "bypass"]]
    invocations += [("04-h2-sweep.sh", [scenario, mode])
                    for scenario in SCENARIOS for mode in ["baseline", "bypass"]]
    env = os.environ.copy()
    for name in ["EXPERIMENT", "PROGRAM_NAME", "EXPERIMENT_LOADS", "EXPERIMENT_SEEDS"]:
        env.pop(name, None)
    env["EXPERIMENT_SOURCE_ROOT"] = str(source)
    tasks = []
    with tempfile.TemporaryDirectory(prefix="h2-plan-") as directory:
        root = Path(directory)
        (root / "common.sh").write_text(PLAN_SHIM)
        for script, args in invocations:
            shutil.copy2(source / "experiments" / script, root / script)
            result = subprocess.run(["bash", str(root / script), *args], env=env,
                                    check=True, capture_output=True)
            fields = result.stdout.split(b"\0")
            if fields.pop() != b"":
                raise ValueError("Malformed experiment plan")
            while fields:
                experiment, run, program = [field.decode() for field in fields[:3]]
                count = int(fields[3])
                options = tuple(field.decode() for field in fields[4:4 + count])
                if len(options) != count:
                    raise ValueError("Incomplete experiment plan")
                del fields[:4 + count]
                tasks.append(Task(experiment, run, program, options, memory_estimate(options, source)))
    if len(tasks) != 315 or len({task.key for task in tasks}) != 315:
        raise ValueError("Expected 315 distinct H2 runs")
    # Make matched comparisons early, while still allowing the queue to backfill
    # from any other scenario when the next task cannot fit available memory.
    order = {"03-load-sweep": 0, **{f"04-h2-{s}": i + 1 for i, s in enumerate(SCENARIOS)}}
    def priority(task):
        options = dict(arg[2:].split("=", 1) for arg in task.args)
        sweep = task.experiment.rsplit("-", 1)[0]
        return (float(options["load"]), order[sweep], int(options["seed"]),
                ["none", "adjacent", "shortest"].index(options.get("bypass", "none")))
    return sorted(tasks, key=priority)


def completed(batch, task):
    directory = batch / task.key
    if not (directory / "summary.txt").is_file():
        return False
    expected = "\n".join(task.args) + "\n"
    if (directory / "args.txt").read_text() != expected:
        raise ValueError(f"Completed run has different options: {task.key}")
    for name in ["flows.csv", "links.csv", "meta.txt"]:
        if not (directory / name).is_file():
            raise ValueError(f"Completed run is missing {name}: {task.key}")
    return True


def memory_available_gb():
    info = Path("/proc/meminfo")
    if not info.exists():
        raise RuntimeError("Memory admission currently requires Linux /proc/meminfo")
    values = dict(re.findall(r"^(\w+):\s+(\d+)", info.read_text(), re.M))
    available = int(values["MemAvailable"]) * 1024
    # The cgroup mount is scoped to this process on the course server/container.
    try:
        maximum = Path("/sys/fs/cgroup/memory.max").read_text().strip()
        current = int(Path("/sys/fs/cgroup/memory.current").read_text())
        if maximum != "max":
            available = min(available, max(0, int(maximum) - current))
    except FileNotFoundError:
        pass
    return available / GIB


def descendant_pids(pid):
    pending, visited = [pid], set()
    while pending:
        child = pending.pop()
        if child in visited:
            continue
        visited.add(child)
        try:
            pending += list(map(int, Path(f"/proc/{child}/task/{child}/children").read_text().split()))
        except (FileNotFoundError, ProcessLookupError):
            pass
    return visited


def process_rss_gb(pid):
    """Sum this worker and descendants, including /usr/bin/time's simulator."""
    rss = 0
    for child in descendant_pids(pid):
        try:
            match = re.search(r"^VmRSS:\s+(\d+)", Path(f"/proc/{child}/status").read_text(), re.M)
            rss += int(match[1]) * 1024 if match else 0
        except (FileNotFoundError, ProcessLookupError):
            pass
    return rss / GIB


class BatchLock:
    def __init__(self, path):
        self.file = path.open("a+")
        try:
            fcntl.flock(self.file, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            self.file.close()
            raise RuntimeError("Another scheduler owns this batch; do not run it twice") from None
        self.file.truncate(0)
        self.file.write(str(os.getpid()) + "\n")
        self.file.flush()

    def close(self):
        self.file.close()


@dataclasses.dataclass
class Worker:
    task: Task
    process: subprocess.Popen
    started: float
    partial: Path


class Scheduler:
    def __init__(self, tasks, batch, source, jobs, memory_gb, reserve_gb=2,
                 report_command=None, available=memory_available_gb, poll_seconds=0.5,
                 provenance_script=None):
        self.tasks, self.batch, self.source = tasks, batch, source
        self.jobs, self.budget, self.reserve = jobs, memory_gb, reserve_gb
        self.report_command, self.available = report_command, available
        self.poll_seconds = poll_seconds
        self.provenance_script = provenance_script
        self.active, self.pending = [], []
        self.finished = self.skipped = self.failed = 0
        self.stopping = False
        self.reporter = None
        self.reported_count = -1
        self.last_report = self.last_progress = 0
        self.env = os.environ.copy()
        self.env["LD_LIBRARY_PATH"] = str(batch / "lib") + (
            ":" + self.env["LD_LIBRARY_PATH"] if self.env.get("LD_LIBRARY_PATH") else "")
        self.env["MPLCONFIGDIR"] = str(batch / "matplotlib-cache")
        # Keep the stop command's batch identifier on all worker descendants.
        self.env["RESULTS_BATCH"] = batch.name

    def log(self, message):
        print(f"[{now()}] {message}", flush=True)

    def request_stop(self, *_):
        self.stopping = True

    def progress(self, state="running", force=False):
        instant = time.monotonic()
        if not force and instant - self.last_progress < 15:
            return
        self.last_progress = instant
        data = dict(updated=now(), status=state, total=len(self.tasks),
                    completed=self.finished + self.skipped, skipped=self.skipped, failed=self.failed,
                    queued=len(self.pending), running=[w.task.key for w in self.active],
                    max_jobs=self.jobs, memory_budget_gb=self.budget,
                    reserved_gb=round(sum(w.task.memory_gb for w in self.active), 2),
                    memory_available_gb=round(self.available(), 2),
                    deferred_for_budget=[t.key for t in self.pending if t.memory_gb > self.budget])
        atomic_text(self.batch / "scheduler-progress.json", json.dumps(data, indent=2) + "\n")
        self.log(f"PROGRESS {data['completed']}/{len(self.tasks)} complete; "
                 f"active={len(self.active)}/{self.jobs} queued={len(self.pending)} "
                 f"reserved={data['reserved_gb']:.1f}/{self.budget:.1f} GiB; {state}")

    def room(self):
        rss = [process_rss_gb(worker.process.pid) for worker in self.active]
        reserved = [worker.task.memory_gb for worker in self.active]
        budget_room = self.budget - sum(max(a, b) for a, b in zip(rss, reserved))
        future_growth = sum(max(0, reservation - actual) for reservation, actual in zip(reserved, rss))
        return min(budget_room, self.available() - self.reserve - future_growth)

    def archive_attempt(self, path, task):
        if path.exists():
            destination = self.batch / "attempts" / task.experiment / task.run
            destination.mkdir(parents=True, exist_ok=True)
            path.rename(destination / (str(time.time_ns()) + path.suffix))

    def start(self, task):
        destination = self.batch / task.key
        partial = destination.with_name(destination.name + ".partial")
        self.archive_attempt(partial, task)
        self.archive_attempt(destination.with_name(destination.name + ".failed"), task)
        partial.mkdir(parents=True)
        (partial / "args.txt").write_text("\n".join(task.args) + "\n")
        identity = (self.batch / "snapshot.ready").read_text().strip()
        (partial / "meta.txt").write_text(
            f"commit: {identity}\nhost: {platform.node()}\nplatform: {platform.system()} {platform.machine()}\n"
            f"started: {now()}\nmemory_reservation_gb: {task.memory_gb:.3f}\n")
        command = ["/usr/bin/time", "-v", str(self.batch / "bin" / task.program), *task.args,
                   "--outputDir=" + str(partial)]
        with (partial / "output.log").open("w") as output, (partial / "resources.log").open("w") as resources:
            process = subprocess.Popen(command, cwd=self.source, env=self.env, stdout=output,
                                       stderr=resources, start_new_session=True)
        self.active.append(Worker(task, process, time.monotonic(), partial))
        self.log(f"START {task.key} pid={process.pid} reservation={task.memory_gb:.2f} GiB")

    def collect(self):
        for worker in list(self.active):
            code = worker.process.poll()
            if code is None:
                continue
            self.active.remove(worker)
            elapsed = time.monotonic() - worker.started
            with (worker.partial / "meta.txt").open("a") as meta:
                meta.write(f"wall_seconds: {elapsed:.3f}\nexit_status: {code}\n")
            success = code == 0 and all((worker.partial / f).is_file()
                                       for f in ["summary.txt", "flows.csv", "links.csv"])
            destination = self.batch / worker.task.key
            if success:
                worker.partial.rename(destination)
                self.finished += 1
                self.log(f"DONE {worker.task.key} in {elapsed:.1f}s")
            else:
                worker.partial.rename(destination.with_name(destination.name + ".failed"))
                self.failed += 1
                self.log(f"FAILED {worker.task.key} exit={code}; see its .failed directory")

    @staticmethod
    def terminate(process):
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass

    def stop_children(self):
        processes = [w.process for w in self.active]
        if self.reporter is not None:
            processes.append(self.reporter)
        descendants = set().union(*(descendant_pids(p.pid) for p in processes))
        for process in processes:
            self.terminate(process)
        deadline = time.monotonic() + 5
        # A time wrapper can exit before its simulator; always signal the group
        # again at the deadline, even if the group leader has already exited.
        while processes and time.monotonic() < deadline:
            if all(process.poll() is not None for process in processes):
                break
            time.sleep(0.05)
        for process in processes:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        # SIGKILL delivery is asynchronous, and a time wrapper may be reaped
        # before its grandchildren finish exiting. Wait for known descendants.
        deadline = time.monotonic() + 2
        while descendants and time.monotonic() < deadline:
            live = set()
            for pid in descendants:
                try:
                    if not re.search(r"^State:\s+Z", Path(f"/proc/{pid}/status").read_text(), re.M):
                        live.add(pid)
                except (FileNotFoundError, ProcessLookupError):
                    pass
            descendants = live
            if descendants:
                time.sleep(0.02)
        self.pending.extend(worker.task for worker in self.active)
        self.active.clear()
        self.reporter = None

    def refresh_report(self, final=False):
        if self.report_command is None:
            return
        if self.reporter is not None:
            if final:
                while (code := self.reporter.poll()) is None:
                    if self.stopping:
                        return
                    time.sleep(0.1)
            else:
                code = self.reporter.poll()
            if code is None:
                return
            self.reporter = None
            if code:
                raise RuntimeError("H2 analysis failed; see analysis/report.log")
        count = self.finished + self.skipped
        if count == self.reported_count:
            return
        if not final and time.monotonic() - self.last_report < 60:
            return
        (self.batch / "analysis").mkdir(exist_ok=True)
        with (self.batch / "analysis/report.log").open("a") as output:
            self.reporter = subprocess.Popen(self.report_command, cwd=self.source, env=self.env,
                                            stdout=output, stderr=subprocess.STDOUT, start_new_session=True)
        self.reported_count, self.last_report = count, time.monotonic()
        if final:
            self.refresh_report(final=True)

    def run(self):
        self.batch.mkdir(parents=True, exist_ok=True)
        lock = BatchLock(self.batch / ".scheduler.lock")
        handlers = {sig: signal.signal(sig, self.request_stop)
                    for sig in [signal.SIGINT, signal.SIGTERM, signal.SIGHUP]}
        status, code = "failed", 1
        try:
            for task in self.tasks:
                if completed(self.batch, task):
                    self.skipped += 1
                else:
                    self.pending.append(task)
            if self.provenance_script is not None:
                directory = self.batch / "scheduler"
                directory.mkdir(exist_ok=True)
                digest = hashlib.sha256(self.provenance_script.read_bytes()).hexdigest()
                shutil.copy2(self.provenance_script, directory / f"h2-queue-{digest[:12]}.py")
                atomic_text(directory / "plan.json", json.dumps(
                    [dataclasses.asdict(task) for task in self.tasks], indent=2) + "\n")
                atomic_text(directory / "config.json", json.dumps(dict(started=now(), scheduler_sha256=digest,
                    jobs=self.jobs, memory_budget_gb=self.budget, reserve_gb=self.reserve), indent=2) + "\n")
            atomic_text(self.batch / "status", "running\n")
            (self.batch / "exit-status").unlink(missing_ok=True)
            self.progress(force=True)
            while self.pending or self.active:
                if self.stopping:
                    status, code = "stopped", 130
                    break
                self.collect()
                if self.failed:
                    break
                while len(self.active) < self.jobs and not self.stopping:
                    room = self.room()
                    task = next((task for task in self.pending if task.memory_gb <= room), None)
                    if task is None:
                        break
                    self.start(task)
                    self.pending.remove(task)
                if self.pending and not self.active and all(task.memory_gb > self.budget for task in self.pending):
                    status, code = "blocked-memory", 2
                    self.log("Remaining tasks exceed the configured memory budget; retained in the queue.")
                    break
                self.refresh_report()
                self.progress()
                time.sleep(self.poll_seconds)
            else:
                status, code = "complete", 0
            if self.stopping:
                status, code = "stopped", 130
            if self.active:
                self.stop_children()
            if code != 130:
                self.refresh_report(final=True)
            if self.stopping:
                status, code = "stopped", 130
        except Exception as error:
            status, code = "failed", 1
            self.log(f"ERROR {error}")
        finally:
            self.stop_children()
            atomic_text(self.batch / "status", status + "\n")
            atomic_text(self.batch / "exit-status", str(code) + "\n")
            self.progress(state=status, force=True)
            self.log(f"{status.upper()} exit={code}")
            for sig, handler in handlers.items():
                signal.signal(sig, handler)
            lock.close()
        return code


def default_jobs():
    cpus = len(os.sched_getaffinity(0)) if hasattr(os, "sched_getaffinity") else (os.cpu_count() or 1)
    try:
        quota, period = Path("/sys/fs/cgroup/cpu.max").read_text().split()
        if quota != "max":
            cpus = min(cpus, max(1, int(quota) // int(period)))
    except FileNotFoundError:
        pass
    return cpus


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("batch", type=Path)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=int(os.environ.get("JOBS", default_jobs())))
    parser.add_argument("--memory-gb", type=float, default=None)
    parser.add_argument("--reserve-gb", type=float, default=2)
    parser.add_argument("--report-python", default=sys.executable)
    parser.add_argument("--dry-run", action="store_true")
    options = parser.parse_args()
    if options.memory_gb is None:
        options.memory_gb = float(os.environ.get("MEMORY_BUDGET_GB", max(0, memory_available_gb() - options.reserve_gb)))
    if (options.jobs < 1 or not math.isfinite(options.memory_gb) or options.memory_gb <= 0
            or not math.isfinite(options.reserve_gb) or options.reserve_gb < 0):
        parser.error("jobs and memory budget must be positive; reserve cannot be negative")
    source, batch = options.source.resolve(), options.batch.resolve()
    tasks = make_plan(source)
    done = [task for task in tasks if completed(batch, task)]
    deferred = [task for task in tasks if task.memory_gb > options.memory_gb and task not in done]
    print(f"H2 plan: {len(tasks)} runs, {len(done)} already completed; jobs={options.jobs}; "
          f"budget={options.memory_gb:.1f} GiB; reserve={options.reserve_gb:.1f} GiB", flush=True)
    groups = {}
    for task in deferred:
        load = next(arg for arg in task.args if arg.startswith("--load="))
        key = (task.experiment.rsplit("-", 1)[0], load, task.memory_gb)
        groups[key] = groups.get(key, 0) + 1
    for (scenario, load, memory), count in groups.items():
        print(f"MEMORY-DEFERRED {scenario} {load}: {count} runs, estimated {memory:.1f} GiB each", flush=True)
    if options.dry_run:
        print("DRY RUN: no builds, simulator processes, or batch files were changed.")
        return 0
    if not (batch / "snapshot.ready").is_file():
        parser.error("run experiments/run-h2.sh first to create a frozen batch")
    for task in tasks:
        if not (batch / "bin" / task.program).is_file():
            parser.error(f"Missing frozen executable: {task.program}")
    script = Path(__file__).resolve()
    command = [options.report_python, str(source / "experiments/analysis/h2-report.py"), str(batch)]
    return Scheduler(tasks, batch, source, options.jobs, options.memory_gb, options.reserve_gb,
                     report_command=command, provenance_script=script).run()


if __name__ == "__main__":
    raise SystemExit(main())
