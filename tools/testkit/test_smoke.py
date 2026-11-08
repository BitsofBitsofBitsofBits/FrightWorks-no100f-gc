"""Tests of tools/smoke-test.py's runner (outcomes, retry, slots, report) with the stand-in game, not the real one."""
import importlib.util
import json
import sys
import threading
import time
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from testkit import slots  # noqa: E402
from testkit._fakes import FAKE, FakeGameCase  # noqa: E402

spec = importlib.util.spec_from_file_location("smoke_test", Path(__file__).resolve().parent.parent / "smoke-test.py")
smoke = importlib.util.module_from_spec(spec)
spec.loader.exec_module(smoke)


class RunnerTest(FakeGameCase):
    def setUp(self):
        super().setUp()
        self.out = Path(self.tmp.name) / "out"
        for sub in ("shots", "logs", "work"):
            (self.out / sub).mkdir(parents=True)
        self.args = types.SimpleNamespace(timeout=6, skip_build_check=True, slots=3)
        self.pool = slots.SlotPool(slots=3, state=Path(self.tmp.name) / "kit", poll=0.05)
        self.saved = dict(smoke.JOBS)
        self.addCleanup(lambda: (smoke.JOBS.clear(), smoke.JOBS.update(self.saved)))

    def fake_job(self, mode, check=None):
        def job(j, attempt):
            game = j.new_game(attempt, boot="h001", src_play=self.src, disc_data=self.disc,
                              game_cmd=[sys.executable, str(FAKE)],
                              extra_env={"FAKE_MODE": mode, "FAKE_STATE_DIR": str(self.state), "FAKE_BOOT_DELAY": "0.3"})
            smoke.common_start(j, game, "h001", attempt)
            if check:
                check(j, game)
            j.finish_attempt(game)
        return job

    def run_fake(self, mode, check=None, name="fake"):
        smoke.JOBS[name] = self.fake_job(mode, check)
        return smoke.run_job(name, self.args, self.out, self.pool, False)

    def test_pass(self):
        r = self.run_fake("normal")
        self.assertEqual(r["outcome"], "passed", r)
        self.assertIn("playable1", r["times"])
        self.assertEqual(self.pool.status()["live"], {})  # the slot was given back

    def test_timeout_then_passed_on_retry(self):
        r = self.run_fake("hang_once")
        self.assertEqual(r["outcome"], "passed on retry", r)
        self.assertIn("timed out", r["detail"])

    def test_two_timeouts_fail(self):
        self.args.timeout = 3
        r = self.run_fake("hang")
        self.assertEqual(r["outcome"], "failed: timeout", r)

    def test_crash_fails_at_once(self):
        r = self.run_fake("crash")
        self.assertEqual(r["outcome"], "failed: crash", r)
        self.assertLess(r["seconds"], 20)  # no retry

    def test_failed_check_is_not_retried(self):
        calls = []

        def check(j, game):
            calls.append(1)
            j.require(False, "something", "wrong")
        r = self.run_fake("normal", check)
        self.assertEqual(r["outcome"], "failed: check", r)
        self.assertEqual(len(calls), 1)
        self.assertIn(("something", "FAILED", "wrong"), [tuple(c) for c in r["checks"]])

    def test_not_run_check_is_marked_and_never_a_pass(self):
        r = self.run_fake("normal", lambda j, g: j.not_run("loop check", "no numpy"))
        self.assertEqual(r["outcome"], "passed (some checks not run)", r)

    def test_error_in_a_job_does_not_stop_others(self):
        def boom(j, game):
            raise ValueError("bug in the test")
        r = self.run_fake("normal", boom)
        self.assertEqual(r["outcome"], "failed: error", r)
        self.assertIn("ValueError", r["detail"])

    def test_report_files(self):
        r = self.run_fake("normal")
        smoke.write_report(self.out, [r], self.args, "build ID test", 12.0)
        data = json.loads((self.out / "results.json").read_text())
        self.assertEqual(data[0]["job"], "fake")
        report = (self.out / "REPORT.md").read_text()
        self.assertIn("| fake | passed |", report)
        self.assertIn("build ID test", report)

    def test_jobs_share_the_slots(self):
        """Four jobs, two slots: never more than two games at once."""
        pool = slots.SlotPool(slots=2, state=Path(self.tmp.name) / "kit2", poll=0.05)
        running, peak, lock = [0], [0], threading.Lock()

        def job(j, attempt):
            with lock:
                running[0] += 1
                peak[0] = max(peak[0], running[0])
            time.sleep(0.5)
            with lock:
                running[0] -= 1
        for n in "abcd":
            smoke.JOBS[n] = job
        threads = [threading.Thread(target=smoke.run_job, args=(n, self.args, self.out, pool, False)) for n in "abcd"]
        [t.start() for t in threads]
        [t.join() for t in threads]
        self.assertEqual(peak[0], 2)


if __name__ == "__main__":
    unittest.main()
