"""Shared game-run slots for automated test runs (Windows and Linux, Python 3.8+, standard library only).

A fixed number of slots (3 unless told otherwise) lets that many automated game runs, from any checkout, share the machine.
`exclusive=True` is for the human check and performance checks: it waits for every slot to be given back, and while it
waits or holds, nobody else gets one. Builds never take a slot.

    pool = SlotPool(slots=3)
    with pool.acquire("smoke h001") as lease:
        ...run the game...
    with pool.acquire("fps measurement", exclusive=True):
        ...the machine to yourself...

The old one-at-a-time lock of tools/debug/kit-common.ps1 (Enter-/Exit-GameLock, game-run.lock in the same folder) is
respected both ways, with no change to the PowerShell scripts:
  * While any slot is held there is a game-run.lock in the kit's format, owned by the slot system (its `root` is the slot
    folder, which no checkout is, and `slots` is true). Its `since` is refreshed by the holders' heartbeat, so the
    PowerShell staleness rule (older than 3 minutes with no game running from `root`) never fires while a holder lives.
    A PowerShell script that wants the lock therefore waits until the last slot is given back.
  * A game-run.lock owned by a PowerShell script makes `acquire` wait, until that script gives it back or it is stale by
    the same rule as Test-GameLockStale (checked by that very function when it is older than 3 minutes).
A holder that dies leaves a slot file that nobody refreshes: it is taken over once its process is gone or its heartbeat is
older than STALE_SECONDS. All changes of the state folder happen under a short-lived mutex (an atomic mkdir).

State folder: %LOCALAPPDATA%\\FrightWorks-kit (the same folder as the old lock), on Linux $XDG_STATE_HOME/FrightWorks-kit or
~/.local/state/FrightWorks-kit; FRIGHTWORKS_KIT_DIR overrides it (tests do).

    python tools/testkit/slots.py status
"""
import atexit
import contextlib
import datetime
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
import uuid
from pathlib import Path

DEFAULT_SLOTS = 3
SLOTS_ENV = "FRIGHTWORKS_TEST_SLOTS"
HEARTBEAT_SECONDS = 15
STALE_SECONDS = 90          # a slot file nobody has refreshed for this long is abandoned
LEGACY_STALE_MINUTES = 3    # kit-common.ps1 Test-GameLockStale
MUTEX_STALE_SECONDS = 10

KIT_COMMON = Path(__file__).resolve().parent.parent / "debug" / "kit-common.ps1"


def state_dir() -> Path:
    override = os.environ.get("FRIGHTWORKS_KIT_DIR")
    if override:
        return Path(override)
    if os.name == "nt":
        base = os.environ.get("LOCALAPPDATA") or str(Path.home() / "AppData" / "Local")
    else:
        base = os.environ.get("XDG_STATE_HOME") or str(Path.home() / ".local" / "state")
    return Path(base) / "FrightWorks-kit"


def slot_count(requested=None) -> int:
    """--slots value, else FRIGHTWORKS_TEST_SLOTS, else 3 (at least 1)."""
    for value in (requested, os.environ.get(SLOTS_ENV)):
        if value not in (None, ""):
            try:
                return max(1, int(value))
            except ValueError:
                raise SystemExit(f"slots: not a number: {value!r}")
    return DEFAULT_SLOTS


def pid_alive(pid) -> bool:
    try:
        pid = int(pid)
    except (TypeError, ValueError):
        return False
    if pid <= 0:
        return False
    if os.name == "nt":
        import ctypes
        kernel32 = ctypes.windll.kernel32
        kernel32.OpenProcess.restype = ctypes.c_void_p
        handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
        if not handle:
            return ctypes.GetLastError() == 5  # access denied: it exists
        try:
            code = ctypes.c_ulong()
            return bool(kernel32.GetExitCodeProcess(ctypes.c_void_p(handle), ctypes.byref(code))) and code.value == 259
        finally:
            kernel32.CloseHandle(ctypes.c_void_p(handle))
    try:
        os.kill(pid, 0)  # POSIX only: signal 0 checks, it never delivers (on Windows it would kill, hence the branch above)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def _read_json(path: Path):
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return data if isinstance(data, dict) else None
    except (OSError, ValueError):
        return None


def _create_json(path: Path, data: dict) -> bool:
    """Create the file only if it does not exist yet. False: somebody else has it."""
    try:
        fd = os.open(str(path), os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    except FileExistsError:
        return False
    with os.fdopen(fd, "w", encoding="utf-8") as f:
        json.dump(data, f)
    return True


def _write_json(path: Path, data: dict) -> None:
    tmp = path.with_name(path.name + ".tmp%d" % os.getpid())
    tmp.write_text(json.dumps(data), encoding="utf-8")
    os.replace(str(tmp), str(path))


def _age(path: Path) -> float:
    try:
        return time.time() - path.stat().st_mtime
    except OSError:
        return 1e9


def _now_iso() -> str:
    return datetime.datetime.now().astimezone().isoformat()


def _parse_iso(text):
    """kit-common.ps1 writes (Get-Date).ToString('o'): 7 fractional digits, which fromisoformat (3.8) rejects."""
    try:
        text = re.sub(r"(\.\d{6})\d+", r"\1", str(text))
        return datetime.datetime.fromisoformat(text)
    except ValueError:
        return None


class Lease:
    """A held slot (or the whole machine, when exclusive). Use as a context manager or call release()."""

    def __init__(self, pool, index, exclusive, why):
        self.pool, self.index, self.exclusive, self.why = pool, index, exclusive, why
        self.waited = 0.0
        self.warned_legacy = False
        self._stop = threading.Event()
        self._released = False
        self._thread = threading.Thread(target=self._beat, name="slot-heartbeat", daemon=True)
        self._thread.start()
        atexit.register(self.release)

    def _beat(self):
        while not self._stop.wait(HEARTBEAT_SECONDS):
            try:
                self.pool._heartbeat(self)
            except OSError:
                pass

    def release(self):
        if self._released:
            return
        self._released = True
        self._stop.set()
        atexit.unregister(self.release)
        try:
            self.pool._release(self)
        except (OSError, TimeoutError):
            pass  # the state folder is gone or locked: nothing left to give back

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.release()


class SlotPool:
    def __init__(self, slots=None, state=None, poll=1.0):
        self.slots = slot_count(slots)
        self.dir = Path(state) if state else state_dir()
        self.slot_dir = self.dir / "slots"
        self.poll = poll
        self.legacy_path = self.dir / "game-run.lock"
        self.exclusive_path = self.dir / "exclusive.lock"
        self.slot_dir.mkdir(parents=True, exist_ok=True)

    # ---- mutex -------------------------------------------------------------------------------------------------
    @contextlib.contextmanager
    def _mutex(self, timeout=30.0):
        path = self.dir / "mutex.d"
        deadline = time.time() + timeout
        while True:
            try:
                os.mkdir(str(path))
                break
            except FileExistsError:
                if _age(path) > MUTEX_STALE_SECONDS:
                    with contextlib.suppress(OSError):
                        os.rmdir(str(path))
                    continue
                if time.time() > deadline:
                    raise TimeoutError(f"slots: could not take the state mutex {path}")
                time.sleep(0.05)
        try:
            yield
        finally:
            with contextlib.suppress(OSError):
                os.rmdir(str(path))

    # ---- state -------------------------------------------------------------------------------------------------
    def _holder_live(self, path: Path):
        """The parsed holder file if its holder still lives, else None (the file is left for the caller to remove)."""
        data = _read_json(path)
        if data is None:
            return {"unreadable": True} if _age(path) < 5 else None  # just created, not written yet
        if not pid_alive(data.get("pid")) or _age(path) > STALE_SECONDS:
            return None
        return data

    def _live_slots(self):
        """{index: data} of the slot files whose holders live; abandoned ones are removed (call under the mutex)."""
        live = {}
        for p in self.slot_dir.glob("slot-*.json"):
            data = self._holder_live(p)
            if data is None:
                with contextlib.suppress(OSError):
                    p.unlink()
                continue
            try:
                live[int(p.stem.split("-")[1])] = data
            except ValueError:
                pass
        return live

    def _exclusive_holder(self):
        data = self._holder_live(self.exclusive_path) if self.exclusive_path.exists() else None
        if data is None and self.exclusive_path.exists():
            with contextlib.suppress(OSError):
                self.exclusive_path.unlink()
        return data

    def _legacy_blocker(self):
        """Description of a PowerShell-owned game-run.lock that is still live, else None."""
        lock = _read_json(self.legacy_path)
        if lock is None:
            return None
        if lock.get("slots"):
            return None
        root = lock.get("root")
        since = _parse_iso(lock.get("since"))
        if not root or not Path(str(root)).exists() or since is None:
            return None  # stale by the PowerShell rule
        age_min = (datetime.datetime.now().astimezone() - since.astimezone()).total_seconds() / 60
        if age_min <= LEGACY_STALE_MINUTES:
            return f"{root} ({lock.get('why')})"
        if os.name == "nt" and KIT_COMMON.exists():
            # exactly the PowerShell rule: no game running from the lock's checkout and older than 3 minutes
            cmd = f". '{KIT_COMMON}'; Test-GameLockStale (Get-GameLock)"
            try:
                out = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", cmd],
                                     capture_output=True, text=True, timeout=60).stdout.strip()
            except (OSError, subprocess.SubprocessError):
                out = ""
            if out.lower() == "true":
                return None
            return f"{root} ({lock.get('why')})"
        return None

    def _ensure_legacy(self, why):
        """Under the mutex, while holding a slot: keep a slot-owned game-run.lock so PowerShell scripts wait."""
        data = self._legacy_record(why)
        if self.legacy_path.exists():
            lock = _read_json(self.legacy_path)
            if lock and lock.get("slots"):
                return
            with contextlib.suppress(OSError):
                self.legacy_path.unlink()  # a stale PowerShell lock (the blocker check passed)
        _create_json(self.legacy_path, data)

    def _drop_legacy_if_unused(self):
        if self._live_slots():
            return
        lock = _read_json(self.legacy_path)
        if lock and lock.get("slots"):
            with contextlib.suppress(OSError):
                self.legacy_path.unlink()

    # ---- acquire / release -------------------------------------------------------------------------------------
    def acquire(self, why, exclusive=False, timeout=None, log=print) -> Lease:
        """Wait for a slot (the whole machine when exclusive) and take it. TimeoutError after `timeout` seconds."""
        start = time.time()
        told = None
        mine_marker = False
        token = uuid.uuid4().hex  # which acquire() call owns the exclusive marker (two in one process must not mix)
        try:
            while True:
                blocker = None
                with self._mutex():
                    live = self._live_slots()
                    marker = self._exclusive_holder()
                    mine = exclusive and bool(marker) and marker.get("token") == token
                    legacy = self._legacy_blocker()
                    if marker and not mine:
                        blocker = f"exclusive run by {marker.get('why')}"
                    elif legacy:
                        blocker = f"old game-run.lock held by {legacy}"
                    elif exclusive:
                        if not marker:
                            if _create_json(self.exclusive_path, self._record(why, "exclusive", None, token)):
                                mine_marker = True
                        elif mine:
                            mine_marker = True
                        if mine_marker and not live:
                            return self._take(0, why, True, start)
                        blocker = f"{len(live)} run(s) in progress, waiting for them to finish ({self._describe(live)})"
                    elif len(live) >= self.slots:
                        blocker = f"all {self.slots} slots busy ({self._describe(live)})"
                    else:
                        index = next(i for i in range(len(live) + 1) if i not in live)
                        return self._take(index, why, False, start)
                if timeout is not None and time.time() - start > timeout:
                    raise TimeoutError(f"slots: waited {timeout:.0f} s for {why}: {blocker}")
                if blocker != told:
                    log(f"slots: {why}: waiting, {blocker}")
                    told = blocker
                time.sleep(self.poll)
        except BaseException:
            if mine_marker:
                with self._mutex():
                    marker = _read_json(self.exclusive_path)
                    if marker and marker.get("token") == token:
                        with contextlib.suppress(OSError):
                            self.exclusive_path.unlink()
            raise

    def _record(self, why, kind, index, token=None):
        return {"pid": os.getpid(), "why": why, "kind": kind, "slot": index, "since": _now_iso(), "root": os.getcwd(),
                "token": token}

    @staticmethod
    def _describe(live):
        return ", ".join(sorted(str(d.get("why", "?")) for d in live.values())) or "none"

    def _take(self, index, why, exclusive, start):
        """Under the mutex: write the slot file and the legacy lock, return the Lease."""
        path = self.slot_dir / f"slot-{index}.json"
        if not _create_json(path, self._record(why, "exclusive" if exclusive else "shared", index)):
            raise RuntimeError(f"slots: {path} appeared under the mutex")
        self._ensure_legacy(why)
        lease = Lease(self, index, exclusive, why)
        lease.waited = time.time() - start
        return lease

    def _heartbeat(self, lease):
        now = time.time()
        for p in (self.slot_dir / f"slot-{lease.index}.json", self.exclusive_path if lease.exclusive else None):
            if p is not None and p.exists():
                os.utime(str(p), (now, now))
        lock = _read_json(self.legacy_path)
        if lock and lock.get("slots"):
            lock["since"] = _now_iso()
            _write_json(self.legacy_path, lock)
        elif lock is None:
            # somebody (an old script's Exit-GameLock, a person) deleted it under us: put it back
            if _create_json(self.legacy_path, self._legacy_record(lease.why)):
                print(f"slots: game-run.lock was removed while {lease.why} held a slot; put back", file=sys.stderr)
        elif not lease.warned_legacy:
            lease.warned_legacy = True
            print(f"slots: game-run.lock is now owned by {lock.get('root')} ({lock.get('why')}) while {lease.why} "
                  "holds a slot: old scripts may overlap", file=sys.stderr)

    def _legacy_record(self, why):
        return {"root": str(self.slot_dir), "why": f"test slots: {why}", "since": _now_iso(), "slots": True}

    def _release(self, lease):
        with self._mutex():
            with contextlib.suppress(OSError):
                (self.slot_dir / f"slot-{lease.index}.json").unlink()
            if lease.exclusive:
                marker = _read_json(self.exclusive_path)
                if marker and marker.get("pid") == os.getpid():
                    with contextlib.suppress(OSError):
                        self.exclusive_path.unlink()
            self._drop_legacy_if_unused()

    # ---- inspection --------------------------------------------------------------------------------------------
    def status(self):
        with self._mutex():
            live = self._live_slots()
            marker = self._exclusive_holder()
        legacy = _read_json(self.legacy_path)
        return {"slots": self.slots, "live": live, "exclusive": marker, "legacy_lock": legacy}


def main(argv):
    if argv[1:] == ["status"]:
        pool = SlotPool()
        st = pool.status()
        print(f"state folder: {pool.dir}")
        print(f"slots: {len(st['live'])} of {st['slots']} in use")
        for index, d in sorted(st["live"].items()):
            print(f"  slot {index}: {d.get('kind')} pid {d.get('pid')} since {d.get('since')}: {d.get('why')}")
        if st["exclusive"]:
            print(f"exclusive: pid {st['exclusive'].get('pid')}: {st['exclusive'].get('why')}")
        if st["legacy_lock"]:
            print(f"game-run.lock: {st['legacy_lock']}")
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
