"""Exercise the dispatcher with lightweight fake workers; never invoke ns-3."""
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import signal
import sys
import tempfile
import threading
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("h2_queue", ROOT / "experiments/h2-queue.py")
queue = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = queue
SPEC.loader.exec_module(queue)

FAKE = '''#!/usr/bin/env python3
import argparse, json, os, signal, subprocess, sys, time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--outputDir', type=Path)
p.add_argument('--name')
p.add_argument('--duration', type=float, default=0.2)
p.add_argument('--fail', action='store_true')
p.add_argument('--child', action='store_true')
p.add_argument('--barrier', type=int, default=0)
a=p.parse_args()
events=Path(os.environ['FAKE_EVENTS'])
with events.open('a') as f: f.write(json.dumps(['start', a.name, time.monotonic()])+'\\n')
if a.barrier:
    deadline=time.monotonic()+5
    while sum(json.loads(line)[0]=='start' for line in events.read_text().splitlines()) < a.barrier:
        if time.monotonic()>deadline: sys.exit(9)
        time.sleep(0.01)
if a.child:
    child=subprocess.Popen([sys.executable, '-c', 'import signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); time.sleep(60)'])
    (a.outputDir/'child.pid').write_text(str(child.pid))
time.sleep(a.duration)
if a.fail: sys.exit(5)
for name in ['summary.txt','flows.csv','links.csv']: (a.outputDir/name).write_text('fixture\\n')
with events.open('a') as f: f.write(json.dumps(['end', a.name, time.monotonic()])+'\\n')
'''


class QueueTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="h2-queue-test-")
        self.addCleanup(self.temporary.cleanup)
        self.batch = Path(self.temporary.name)
        self.source = self.batch / "source"
        self.source.mkdir()
        (self.batch / "bin").mkdir()
        program = self.batch / "bin/fake"
        program.write_text(FAKE)
        program.chmod(0o755)
        (self.batch / "snapshot.ready").write_text("fixture\n")
        self.events = self.batch / "events.jsonl"
        self.previous_events = os.environ.get("FAKE_EVENTS")
        os.environ["FAKE_EVENTS"] = str(self.events)
        self.addCleanup(self.restore_env)

    def restore_env(self):
        if self.previous_events is None:
            os.environ.pop("FAKE_EVENTS", None)
        else:
            os.environ["FAKE_EVENTS"] = self.previous_events

    def task(self, name, memory=1, duration=0.2, extra=(), experiment="scenario-baseline"):
        return queue.Task(experiment, name, "fake", ("--name=" + name, "--duration=" + str(duration), *extra), memory)

    def scheduler(self, tasks, **options):
        kwargs = dict(jobs=2, memory_gb=4, reserve_gb=0, available=lambda: 100, poll_seconds=0.02)
        kwargs.update(options)
        return queue.Scheduler(tasks, self.batch, self.source, **kwargs)

    def run_queue(self, scheduler):
        with contextlib.redirect_stdout(io.StringIO()):
            return scheduler.run()

    def records(self):
        return [json.loads(line) for line in self.events.read_text().splitlines()]

    def test_cross_scenario_parallelism_and_resume(self):
        tasks = [self.task("a", experiment="one-baseline"), self.task("b", experiment="two-bypass"), self.task("c")]
        self.assertEqual(self.run_queue(self.scheduler(tasks)), 0)
        records = self.records()
        first_end = next(i for i, row in enumerate(records) if row[0] == "end")
        self.assertEqual({row[1] for row in records[:first_end]}, {"a", "b"})
        running = peak = 0
        for event, _, _ in records:
            running += 1 if event == "start" else -1
            peak = max(peak, running)
        self.assertEqual(peak, 2)
        before = self.events.read_bytes()
        self.assertEqual(self.run_queue(self.scheduler(tasks)), 0)
        self.assertEqual(self.events.read_bytes(), before)
        state = json.loads((self.batch / "scheduler-progress.json").read_text())
        self.assertEqual(state["skipped"], 3)
        self.assertEqual(state["completed"], 3)

    def test_options_mismatch_is_rejected(self):
        task = self.task("a")
        self.assertEqual(self.run_queue(self.scheduler([task])), 0)
        changed = self.task("a", duration=0.3)
        self.assertEqual(self.run_queue(self.scheduler([changed])), 1)
        self.assertEqual(len(self.records()), 2)
        self.assertEqual((self.batch / "status").read_text().strip(), "failed")

    def test_eight_workers_can_run_concurrently(self):
        tasks = [self.task(str(i), extra=("--barrier=8",)) for i in range(8)]
        self.assertEqual(self.run_queue(self.scheduler(tasks, jobs=8, memory_gb=12)), 0)
        records = self.records()
        first_end = next(i for i, row in enumerate(records) if row[0] == "end")
        self.assertEqual(first_end, 8)
        self.assertEqual(len(records), 16)

    def test_memory_backfill_and_unschedulable_status(self):
        tasks = [self.task("huge", memory=8), self.task("a", memory=2), self.task("b", memory=2)]
        self.assertEqual(self.run_queue(self.scheduler(tasks, memory_gb=3)), 2)
        self.assertEqual([row[1] for row in self.records() if row[0] == "start"], ["a", "b"])
        self.assertEqual((self.batch / "status").read_text().strip(), "blocked-memory")
        self.assertFalse((self.batch / "scenario-baseline/huge.partial").exists())

    def test_current_free_memory_delays_admission(self):
        readings = iter([0, 0, 0, 0, 100])
        available = lambda: next(readings, 100)
        self.assertEqual(self.run_queue(self.scheduler([self.task("a")], available=available)), 0)

    def test_partial_output_is_preserved_on_retry(self):
        task = self.task("a")
        partial = self.batch / (task.key + ".partial")
        partial.mkdir(parents=True)
        (partial / "old-output").write_text("keep me")
        self.assertEqual(self.run_queue(self.scheduler([task])), 0)
        self.assertEqual(len(list((self.batch / "attempts").rglob("old-output"))), 1)

    def test_failed_worker_prevents_false_completion(self):
        tasks = [self.task("bad", duration=0.05, extra=("--fail",)), self.task("running", duration=20), self.task("queued")]
        self.assertEqual(self.run_queue(self.scheduler(tasks)), 1)
        self.assertTrue((self.batch / "scenario-baseline/bad.failed").exists())
        self.assertFalse((self.batch / "scenario-baseline/queued.partial").exists())
        self.assertEqual((self.batch / "status").read_text().strip(), "failed")

    def test_stop_cleans_up_grandchildren_and_preserves_partial(self):
        task = self.task("long", duration=60, extra=("--child",))
        scheduler = self.scheduler([task])
        def stop_when_child_started():
            child_file = self.batch / (task.key + ".partial/child.pid")
            for _ in range(100):
                if child_file.exists():
                    scheduler.request_stop()
                    return
                time.sleep(0.02)
            scheduler.request_stop()
        timer = threading.Thread(target=stop_when_child_started)
        timer.start()
        self.assertEqual(self.run_queue(scheduler), 130)
        timer.join()
        partial = self.batch / (task.key + ".partial")
        self.assertTrue(partial.exists())
        pid = int((partial / "child.pid").read_text())
        status_file = Path(f"/proc/{pid}/status")
        if status_file.exists():
            # An orphan can briefly remain as a zombie until init reaps it.
            self.assertIn("\nState:\tZ", status_file.read_text())
        self.assertEqual((self.batch / "status").read_text().strip(), "stopped")

    def test_sighup_stops_worker_instead_of_orphaning_it(self):
        scheduler = self.scheduler([self.task("hangup", duration=60)])
        def send_hangup():
            for _ in range(100):
                if self.events.exists():
                    os.kill(os.getpid(), signal.SIGHUP)
                    return
                time.sleep(0.02)
            scheduler.request_stop()
        thread = threading.Thread(target=send_hangup)
        thread.start()
        self.assertEqual(self.run_queue(scheduler), 130)
        thread.join()
        self.assertEqual((self.batch / "status").read_text().strip(), "stopped")

    def test_batch_lock_rejects_duplicate_scheduler(self):
        lock = queue.BatchLock(self.batch / ".scheduler.lock")
        try:
            with self.assertRaises(RuntimeError):
                self.scheduler([self.task("a")]).run()
        finally:
            lock.close()
        self.assertFalse(self.events.exists())

    def test_report_failure_is_not_marked_complete(self):
        scheduler = self.scheduler([self.task("a")], report_command=[sys.executable, "-c", "raise SystemExit(1)"])
        self.assertEqual(self.run_queue(scheduler), 1)
        self.assertEqual((self.batch / "status").read_text().strip(), "failed")


class PlanTests(unittest.TestCase):
    def test_default_uses_available_cpus_without_four_worker_cap(self):
        with mock.patch.object(queue.os, "sched_getaffinity", return_value=set(range(16))), \
                mock.patch.object(queue.Path, "read_text", side_effect=FileNotFoundError):
            self.assertEqual(queue.default_jobs(), 16)

    def test_default_still_respects_cpu_quota(self):
        with mock.patch.object(queue.os, "sched_getaffinity", return_value=set(range(16))), \
                mock.patch.object(queue.Path, "read_text", return_value="200000 100000"):
            self.assertEqual(queue.default_jobs(), 2)

    def test_plan_has_exactly_315_unique_runs(self):
        tasks = queue.make_plan(ROOT)
        self.assertEqual(len(tasks), 315)
        self.assertEqual(len({task.key for task in tasks}), 315)
        self.assertEqual(sum(t.program == "tcp-experiment" for t in tasks), 105)
        heavy = next(t for t in tasks if t.experiment == "04-h2-fraction99-baseline" and "--load=0.7" in t.args)
        self.assertGreater(heavy.memory_gb, 30)

    def test_old_common_is_not_sourced_and_plan_does_not_launch_workers(self):
        with tempfile.TemporaryDirectory(prefix="h2-old-snapshot-") as temporary:
            source = Path(temporary)
            files = ["experiments/03-load-sweep-baseline.sh", "experiments/03-load-sweep-bypass.sh",
                     "experiments/03-load-sweep-settings.sh", "experiments/04-h2-sweep.sh",
                     "src/tcp/tcp-experiment.h", "experiments/workloads/websearch.cdf",
                     "experiments/workloads/hadoop.cdf"]
            for filename in files:
                path = source / filename
                path.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(ROOT / filename, path)
            (source / "experiments/common.sh").write_text("echo 'must not be sourced' >&2; exit 99\n")
            tasks = queue.make_plan(source)
            self.assertEqual(len(tasks), 315)
            self.assertFalse((source / "experiments/results").exists())


if __name__ == "__main__":
    unittest.main()
