"""Client of the game's scripted dev menu (Python 3.8+, standard library only). Needs the game started with the dev
tools on (launch.py does that) and its working directory (the Play copy) given here.

  * devmenu.txt, one command per line, read by the game every 30 frames and then deleted. The game leaves the file alone
    until it is in play, except for `skip` and `scene` (see PollCommandFile in no100f_devmenu.cpp). Each answer is
    appended to devmenu.log as `<the command line> -> <status>`; a status may run over several lines.
  * devmenu_press.txt, `BUTTON[+BUTTON...] [FRAMES] [cutscene]` per line: presses on pad 1 for that many frames (default
    10), also during a cutscene. `cutscene` = only if a recorded cutscene is playing then. Each is answered in
    devmenu.log as `press <line> -> queued | skipped (no cutscene) | unknown button`.

    dm = DevMenu(play_dir)
    dm.command("where")                -> "h001 12.000 0.121 3.500 yaw 90.0"
    dm.command("cutscene")             -> "cutscene no control yes"
    dm.press("a 10 cutscene")          -> "queued"
"""
import os
import re
import time
from pathlib import Path


class DevMenuTimeout(Exception):
    """The game did not answer in time (it is probably not in play yet, or hung)."""


class DevMenu:
    def __init__(self, play_dir, crash_check=None, poll=0.2):
        self.dir = Path(play_dir)
        self.crash_check = crash_check or (lambda: None)
        self.poll = poll

    @property
    def log_path(self):
        return self.dir / "devmenu.log"

    def _log_size(self):
        try:
            return self.log_path.stat().st_size
        except OSError:
            return 0

    def _log_since(self, offset):
        try:
            with open(self.log_path, "rb") as f:
                f.seek(offset)
                return f.read().decode("utf-8", errors="replace").replace("\r\n", "\n")  # text mode on Windows
        except OSError:
            return ""

    def _sleep_checked(self):
        self.crash_check()
        time.sleep(self.poll)

    def _write(self, name, lines, deadline):
        """Write the command file when the game has taken the previous one; atomically (rename)."""
        target = self.dir / name
        while target.exists():
            if time.time() > deadline:
                raise DevMenuTimeout(f"{name} still not taken by the game")
            self._sleep_checked()
        tmp = self.dir / (name + ".tmp")
        tmp.write_text("\n".join(lines) + "\n", encoding="utf-8")
        while True:
            try:
                os.replace(str(tmp), str(target))
                return
            except PermissionError:
                if time.time() > deadline:
                    raise DevMenuTimeout(f"could not write {name}")
                time.sleep(0.05)

    def _answers(self, offset, prefixes):
        """The status texts for each prefix in order, if all of them are in the log after `offset`; else None."""
        text = self._log_since(offset)
        found, pos = [], 0
        for prefix in prefixes:
            m = re.compile(r"(?m)^" + re.escape(prefix) + r" -> ").search(text, pos)
            if not m:
                return None
            found.append(m)
            pos = m.end()
        out = []
        for i, m in enumerate(found):
            end = found[i + 1].start() if i + 1 < len(found) else len(text)
            out.append(text[m.end():end].rstrip("\r\n"))
        # the last answer may still be being written: need its line to be complete
        return out if text.endswith("\n") else None

    def commands(self, lines, timeout=30):
        """Send several command lines at once; returns their statuses. DevMenuTimeout if not all are answered."""
        deadline = time.time() + timeout
        offset = self._log_size()
        self._write("devmenu.txt", lines, deadline)
        while True:
            self.crash_check()
            answers = self._answers(offset, [l.strip() for l in lines])
            if answers is not None:
                return answers
            if time.time() > deadline:
                raise DevMenuTimeout(f"no answer to {lines!r} in {timeout:.0f} s")
            time.sleep(self.poll)

    def command(self, line, timeout=30):
        return self.commands([line], timeout)[0]

    def press(self, spec, timeout=30):
        """Queue a press (`a 10`, `a 10 cutscene`, `start`, `down`...); returns how the game answered."""
        deadline = time.time() + timeout
        offset = self._log_size()
        self._write("devmenu_press.txt", [spec], deadline)
        while True:
            self.crash_check()
            answers = self._answers(offset, [f"press {spec.strip()}"])
            if answers is not None:
                return answers[0]
            if time.time() > deadline:
                raise DevMenuTimeout(f"no answer to press {spec!r} in {timeout:.0f} s")
            time.sleep(self.poll)

    # -- the answers the smoke test reads
    def where(self, timeout=30):
        """(scene, x, y, z, yaw) from `where`, or None when the game says the player is not found."""
        text = self.command("where", timeout)
        m = re.match(r"(\w+) (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) yaw (-?[\d.]+)", text)
        if not m:
            return None
        return (m.group(1), *(float(m.group(i)) for i in range(2, 6)))

    def cutscene(self, timeout=10):
        """(cutscene_playing, player_has_control) from `cutscene`."""
        text = self.command("cutscene", timeout)
        m = re.search(r"cutscene (yes|no) control (yes|no)", text)
        if not m:
            raise DevMenuTimeout(f"unexpected answer to cutscene: {text!r}")
        return m.group(1) == "yes", m.group(2) == "yes"
