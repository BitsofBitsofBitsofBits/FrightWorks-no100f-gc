"""Tests of devmenu.py and waitplay.py against the stand-in game: python -m unittest discover -s tools/testkit -p "test_*.py" """
import sys
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from testkit import waitplay  # noqa: E402
from testkit.devmenu import DevMenuTimeout  # noqa: E402
from testkit.launch import GameCrashed  # noqa: E402
from testkit._fakes import FakeGameCase  # noqa: E402

QUIET = lambda *a, **k: None  # noqa: E731


class DevMenuTest(FakeGameCase):
    def test_commands_answers_and_multiline_status(self):
        g = self.game(env={"FAKE_CUTSCENE": "0"}).start()
        waitplay.wait_until_playable(g, "h001", timeout=20, log=QUIET)
        dm = g.devmenu
        self.assertEqual(dm.where()[0], "h001")
        self.assertEqual(dm.commands(["snacks 7", "player"], 10),
                         ["spendable snacks set to 7", "player health=3/5 snacks=7\n  second line"])
        dm.command("tp -25.618 1.186 -1.515", 10)
        scene, x, y, z, yaw = dm.where()
        self.assertEqual((scene, x, y, z), ("h001", -25.618, 1.186, -1.515))

    def test_press_is_answered_and_skipped_without_a_cutscene(self):
        g = self.game(env={"FAKE_CUTSCENE": "0"}).start()
        waitplay.wait_until_playable(g, "h001", timeout=20, log=QUIET)
        self.assertEqual(g.devmenu.press("a 10", 10), "queued")
        self.assertEqual(g.devmenu.press("a 10 cutscene", 10), "skipped (no cutscene)")

    def test_no_answer_while_not_in_play_is_a_timeout_not_a_hang(self):
        g = self.game(mode="hang").start()
        with self.assertRaises(DevMenuTimeout):
            g.devmenu.command("where", timeout=1.0)


class WaitPlayableTest(FakeGameCase):
    def test_skips_the_cutscene_and_returns_when_control_is_back(self):
        g = self.game().start()
        r = waitplay.wait_until_playable(g, "h001", timeout=30, log=QUIET)
        self.assertGreaterEqual(r["presses"], 1)
        self.assertTrue(r["where"].startswith("h001"))

    def test_no_cutscene_means_no_presses(self):
        g = self.game(env={"FAKE_CUTSCENE": "0"}).start()
        r = waitplay.wait_until_playable(g, "h001", timeout=30, log=QUIET)
        self.assertEqual(r["presses"], 0)

    def test_skip_button_none_never_presses(self):
        g = self.game(env={"FAKE_CUTSCENE": "1"}).start()
        with self.assertRaises(waitplay.PlayTimeout):
            waitplay.wait_until_playable(g, "h001", timeout=3, skip_button=None, log=QUIET)

    def test_timeout_reports_the_last_state(self):
        g = self.game(mode="hang").start()
        t = time.time()
        with self.assertRaises(waitplay.PlayTimeout) as ctx:
            waitplay.wait_until_playable(g, "h001", timeout=3, log=QUIET)
        self.assertLess(time.time() - t, 15)
        self.assertIn("not in play", str(ctx.exception))

    def test_crash_fails_at_once(self):
        g = self.game(mode="crash", env={"FAKE_BOOT_DELAY": "30"}).start()
        t = time.time()
        with self.assertRaises(GameCrashed) as ctx:
            waitplay.wait_until_playable(g, "h001", timeout=60, log=QUIET)
        self.assertLess(time.time() - t, 15)
        self.assertIn("code 3", str(ctx.exception))

    def test_a_logged_fault_fails_at_once(self):
        g = self.game(mode="fault", env={"FAKE_BOOT_DELAY": "30"}).start()
        with self.assertRaises(GameCrashed) as ctx:
            waitplay.wait_until_playable(g, "h001", timeout=60, log=QUIET)
        self.assertIn("AccessViolation", str(ctx.exception))

    def test_level_that_never_loads_times_out(self):
        g = self.game().start()
        with self.assertRaises(waitplay.PlayTimeout) as ctx:
            waitplay.wait_until_playable(g, "zzzz", timeout=2, log=QUIET)
        self.assertIn("never loaded", str(ctx.exception))


class RetryTest(FakeGameCase):
    def attempt(self, mode):
        def run(n):
            g = self.game(f"try{n}", mode=mode).start()
            return waitplay.wait_until_playable(g, "h001", timeout=4 if mode != "normal" else 30, log=QUIET)
        return run

    def test_first_try_passes(self):
        outcome, result, notes = waitplay.retry_on_timeout(self.attempt("normal"), log=QUIET)
        self.assertEqual((outcome, notes), ("passed", []))

    def test_one_timeout_then_passed_on_retry(self):
        outcome, result, notes = waitplay.retry_on_timeout(self.attempt("hang_once"), log=QUIET)
        self.assertEqual(outcome, "passed on retry")
        self.assertIn("timed out", notes[0])

    def test_two_timeouts_fail(self):
        calls = []

        def run(n):
            calls.append(n)
            return self.attempt("hang")(n)
        with self.assertRaises(waitplay.PlayTimeout):
            waitplay.retry_on_timeout(run, log=QUIET)
        self.assertEqual(calls, [1, 2])

    def test_a_crash_is_not_retried(self):
        calls = []

        def run(n):
            calls.append(n)
            return self.attempt("crash")(n)
        with self.assertRaises(GameCrashed):
            waitplay.retry_on_timeout(run, log=QUIET)
        self.assertEqual(calls, [1])

    def test_other_failures_are_not_retried(self):
        calls = []

        def run(n):
            calls.append(n)
            raise AssertionError("check failed")
        with self.assertRaises(AssertionError):
            waitplay.retry_on_timeout(run, log=QUIET)
        self.assertEqual(calls, [1])


if __name__ == "__main__":
    unittest.main()
