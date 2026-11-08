"""Tests of slots.py with no game: python -m unittest discover -s tools/testkit -p "test_*.py" """
import datetime
import json
import os
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from testkit import slots  # noqa: E402

QUIET = lambda *a, **k: None  # noqa: E731


def old_time(minutes):
    return (datetime.datetime.now().astimezone() - datetime.timedelta(minutes=minutes)).isoformat()


class SlotsTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.state = Path(self.tmp.name) / "FrightWorks-kit"
        self.state.mkdir()
        self.checkout = Path(self.tmp.name) / "checkout"
        self.checkout.mkdir()

    def pool(self, n=3):
        return slots.SlotPool(slots=n, state=self.state, poll=0.05)

    def test_n_slots_then_blocked_then_released(self):
        pool = self.pool(3)
        leases = [pool.acquire(f"job{i}", log=QUIET) for i in range(3)]
        self.assertEqual(sorted(l.index for l in leases), [0, 1, 2])
        with self.assertRaises(TimeoutError):
            pool.acquire("fourth", timeout=0.3, log=QUIET)
        leases[1].release()
        fourth = pool.acquire("fourth", timeout=2, log=QUIET)
        self.assertEqual(fourth.index, 1)
        for l in leases + [fourth]:
            l.release()
        self.assertEqual(pool.status()["live"], {})

    def test_slot_count_argument_env_and_default(self):
        self.assertEqual(slots.slot_count(None), 3 if not os.environ.get(slots.SLOTS_ENV) else int(os.environ[slots.SLOTS_ENV]))
        self.assertEqual(slots.slot_count(5), 5)
        self.assertEqual(slots.slot_count("2"), 2)
        self.assertEqual(slots.slot_count(0), 1)

    def test_abandoned_slot_is_taken_over(self):
        pool = self.pool(1)
        dead = {"pid": 2 ** 22 + 12345, "why": "crashed", "kind": "shared", "slot": 0, "since": old_time(1)}
        (self.state / "slots" / "slot-0.json").write_text(json.dumps(dead))
        lease = pool.acquire("next", timeout=2, log=QUIET)
        self.assertEqual(lease.index, 0)
        lease.release()

    def test_unrefreshed_slot_of_a_live_pid_is_taken_over(self):
        pool = self.pool(1)
        stuck = {"pid": os.getpid(), "why": "hung", "kind": "shared", "slot": 0, "since": old_time(5)}
        p = self.state / "slots" / "slot-0.json"
        p.write_text(json.dumps(stuck))
        past = time.time() - slots.STALE_SECONDS - 5
        os.utime(str(p), (past, past))
        pool.acquire("next", timeout=2, log=QUIET).release()

    def test_exclusive_waits_for_shared_and_blocks_new_ones(self):
        pool = self.pool(3)
        shared = pool.acquire("shared", log=QUIET)
        got = {}

        def want_exclusive():
            got["lease"] = pool.acquire("human check", exclusive=True, timeout=10, log=QUIET)

        t = threading.Thread(target=want_exclusive)
        t.start()
        time.sleep(0.4)
        self.assertNotIn("lease", got)  # still waiting for the shared run
        with self.assertRaises(TimeoutError):  # and while it waits nobody new gets in
            pool.acquire("late", timeout=0.3, log=QUIET)
        shared.release()
        t.join(5)
        self.assertIn("lease", got)
        with self.assertRaises(TimeoutError):
            pool.acquire("while exclusive", timeout=0.3, log=QUIET)
        got["lease"].release()
        pool.acquire("after", timeout=2, log=QUIET).release()
        self.assertFalse((self.state / "exclusive.lock").exists())

    def test_timed_out_exclusive_leaves_no_marker(self):
        pool = self.pool(3)
        shared = pool.acquire("shared", log=QUIET)
        with self.assertRaises(TimeoutError):
            pool.acquire("human", exclusive=True, timeout=0.3, log=QUIET)
        self.assertFalse((self.state / "exclusive.lock").exists())
        pool.acquire("another", timeout=1, log=QUIET).release()
        shared.release()

    # ---- the old PowerShell lock ------------------------------------------------------------------------------------
    def legacy(self):
        return json.loads((self.state / "game-run.lock").read_text())

    def test_holding_a_slot_writes_a_slot_owned_legacy_lock_that_is_not_a_checkout(self):
        pool = self.pool(3)
        a = pool.acquire("a", log=QUIET)
        lock = self.legacy()
        self.assertTrue(lock["slots"])
        self.assertEqual(Path(lock["root"]), self.state / "slots")  # exists, and no checkout's root equals it
        self.assertTrue(Path(lock["root"]).is_dir())
        b = pool.acquire("b", log=QUIET)
        a.release()
        self.assertTrue((self.state / "game-run.lock").exists())  # b still holds
        b.release()
        self.assertFalse((self.state / "game-run.lock").exists())

    def test_powershell_owned_lock_blocks_until_removed(self):
        pool = self.pool(3)
        (self.state / "game-run.lock").write_text(json.dumps(
            {"root": str(self.checkout), "why": "manual-session start", "since": slots._now_iso()}))
        with self.assertRaises(TimeoutError):
            pool.acquire("smoke", timeout=0.3, log=QUIET)
        (self.state / "game-run.lock").unlink()
        pool.acquire("smoke", timeout=2, log=QUIET).release()

    def test_stale_powershell_lock_is_taken_over(self):
        pool = self.pool(3)
        (self.state / "game-run.lock").write_text(json.dumps(
            {"root": str(self.tmp.name + "/gone"), "why": "died", "since": old_time(30)}))
        lease = pool.acquire("smoke", timeout=3, log=QUIET)
        self.assertTrue(self.legacy()["slots"])
        lease.release()

    def test_parses_the_seven_digit_timestamps_powershell_writes(self):
        parsed = slots._parse_iso("2026-11-07T17:01:54.1234567+01:00")
        self.assertIsNotNone(parsed)
        self.assertEqual(parsed.microsecond, 123456)

    def test_heartbeat_puts_back_a_deleted_legacy_lock_and_refreshes_since(self):
        pool = self.pool(3)
        lease = pool.acquire("a", log=QUIET)
        first = self.legacy()["since"]
        (self.state / "game-run.lock").unlink()  # an old script's Exit-GameLock, or a person
        pool._heartbeat(lease)
        self.assertTrue(self.legacy()["slots"])
        time.sleep(0.02)
        pool._heartbeat(lease)
        self.assertGreater(self.legacy()["since"], first)
        lease.release()

    def test_heartbeat_does_not_overwrite_a_lock_somebody_else_took(self):
        pool = self.pool(3)
        lease = pool.acquire("a", log=QUIET)
        other = {"root": str(self.checkout), "why": "play-patched", "since": slots._now_iso()}
        (self.state / "game-run.lock").write_text(json.dumps(other))
        pool._heartbeat(lease)
        self.assertEqual(self.legacy(), other)
        lease.release()
        self.assertEqual(self.legacy(), other)  # and the release does not delete a lock that is not ours

    @unittest.skipUnless(os.name == "nt", "kit-common.ps1 is PowerShell on Windows")
    def test_real_powershell_scripts_and_slots_exclude_each_other(self):
        """The real Enter-GameLock / Exit-GameLock of kit-common.ps1, against the slot-owned lock and the other way."""
        env = dict(os.environ, LOCALAPPDATA=self.tmp.name)  # kit-common keeps its lock in $env:LOCALAPPDATA\FrightWorks-kit
        kit = slots.KIT_COMMON

        def ps(command):
            return subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command",
                                   f". '{kit}'; {command}"], capture_output=True, text=True, env=env, timeout=120)

        pool = self.pool(3)
        lease = pool.acquire("smoke", log=QUIET)
        # a script from any checkout, this one's included, waits (WaitMinutes 0 = give up at once)
        r = ps(f"Enter-GameLock '{self.checkout}' 'manual-session' 0")
        self.assertNotEqual(r.returncode, 0, r.stdout)
        self.assertIn("still held by", r.stdout + r.stderr)
        lease.release()
        r = ps(f"Enter-GameLock '{self.checkout}' 'manual-session' 0")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with self.assertRaises(TimeoutError):  # and now the slots wait for the script
            pool.acquire("smoke", timeout=0.5, log=QUIET)
        r = ps(f"Exit-GameLock '{self.checkout}'")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        pool.acquire("smoke", timeout=3, log=QUIET).release()


class ConcurrencyTest(unittest.TestCase):
    def test_processes_never_exceed_the_slot_count(self):
        with tempfile.TemporaryDirectory() as tmp:
            state, counter = Path(tmp) / "kit", Path(tmp) / "running"
            counter.mkdir()
            child = (
                "import sys, time, os; sys.path.insert(0, sys.argv[1])\n"
                "from testkit import slots\n"
                "pool = slots.SlotPool(slots=3, state=sys.argv[2], poll=0.05)\n"
                "with pool.acquire('child %d' % os.getpid(), log=lambda *a: None):\n"
                "    open(os.path.join(sys.argv[3], str(os.getpid())), 'w').close()\n"
                "    seen = len(os.listdir(sys.argv[3]))\n"
                "    time.sleep(0.4)\n"
                "    os.remove(os.path.join(sys.argv[3], str(os.getpid())))\n"
                "print(seen)\n")
            root = str(Path(__file__).resolve().parent.parent)
            procs = [subprocess.Popen([sys.executable, "-c", child, root, str(state), str(counter)],
                                      stdout=subprocess.PIPE, text=True) for _ in range(8)]
            seen = [int(p.communicate(timeout=60)[0]) for p in procs]
            self.assertTrue(all(p.returncode == 0 for p in procs))
            self.assertLessEqual(max(seen), 3)
            self.assertEqual(max(seen), 3)  # and they did overlap


if __name__ == "__main__":
    unittest.main()
