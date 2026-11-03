"""verdicts.py [--reset]: sync the dev menu's per-level Fine/Bugged/Fixed marks with suspects.csv. Run with the game
closed (the menu reads Play/UserData/devmenu_verdicts.txt once and rewrites it on every mark).

The menu file has one line per mark: id, scene, status. The record of the marks is scene_verdicts.csv (id, scene,
status, date): the menu's marks are added to it, marks unmarked in the menu are dropped. "fixed" is only what the human
marks (hunt sessions run the unfixed HD pack, so a texture fixed in cleanpack\ can still be "bugged" there).
suspects.csv gets a summary per texture: bugged once it is bugged in any level, fine once it is fine in every level that
uses it (menus and boot don't count), otherwise suspect; the note starts with "<mark> in <scenes> (dev menu)" per mark.
confirmed* is kept. Then the menu file is rewritten from the record.

--reset: start over (2026-10-25: marks became per level). Every status fine in suspects.csv goes back to suspect (old
note kept after "reset <date>:"), the record is emptied, and the T### spots in devmenu_spots.txt are removed (backup in
spots-backup-<date>.txt; other spots stay).
"""
import csv, datetime, os, re, shutil, subprocess, sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")  # repo root
HERE = os.path.join(ROOT, "autotest", "texhunt")  # git-ignored work data and output
CSV = os.path.join(HERE, "suspects.csv")
RECORD = os.path.join(HERE, "scene_verdicts.csv")
USERDATA = os.path.join(ROOT, "Play", "UserData")
MENU = os.path.join(USERDATA, "devmenu_verdicts.txt")
SPOTS = os.path.join(USERDATA, "devmenu_spots.txt")
TAG = "(dev menu)"
MARKS = ("fine", "bugged", "fixed")  # statuses the menu's verdict button sets
TODAY = datetime.date.today().isoformat()

tasks = subprocess.run(["tasklist", "/FI", "IMAGENAME eq FrightWorks Game.exe", "/NH"], capture_output=True, text=True)
if "Scooby" in tasks.stdout:  # tasklist cuts names at 25 characters
    sys.exit("game is running: close it first (the menu would overwrite the file)")

with open(CSV, newline="", encoding="utf-8") as fh:
    rows = list(csv.reader(fh))
head, body = rows[0], rows[1:]
ID, STATUS, NOTE, SCENES = (head.index(k) for k in ("id", "status", "note", "scenes"))
record = {}  # (id, scene) -> (status, date)
if os.path.exists(RECORD):
    with open(RECORD, newline="", encoding="utf-8") as fh:
        for r in csv.DictReader(fh):
            record[(r["id"], r["scene"])] = (r["status"], r["date"])
log = []

if "--reset" in sys.argv:
    for r in body:
        if r[STATUS] == "fine":
            r[STATUS] = "suspect"
            r[NOTE] = f"reset {TODAY}: {r[NOTE]}" if r[NOTE] else ""
            log.append(f"{r[ID]} fine -> suspect")
    record.clear()
    if os.path.exists(SPOTS):
        shutil.copy(SPOTS, os.path.join(HERE, f"spots-backup-{TODAY}.txt"))
        lines = open(SPOTS, encoding="utf-8").read().splitlines()
        keep = [l for l in lines if not re.fullmatch(r"T\d{3}", l.split(" ", 5)[-1].strip())]
        with open(SPOTS, "w", encoding="utf-8", newline="\n") as fh:
            fh.write("".join(l + "\n" for l in keep))
        log.append(f"spots: removed {len(lines) - len(keep)} T### spots, kept {len(keep)}")
elif os.path.exists(MENU):  # pull the menu's marks (a missing file is not "everything unmarked")
    menu = {}
    for line in open(MENU, encoding="utf-8"):
        f = line.rstrip("\r\n").split("\t")
        if line.startswith("#") or len(f) < 3: continue
        if f[2] in MARKS and f[1] != "*": menu[(f[0], f[1])] = f[2]  # "*" = old all-levels "fixed" line
    for key, st in menu.items():
        if record.get(key, ("",))[0] != st:
            record[key] = (st, TODAY); log.append(f"{key[0]} {st} in {key[1]}")
    for key in [k for k, v in record.items() if v[0] in MARKS and k not in menu]:
        del record[key]; log.append(f"{key[0]} unmarked in {key[1]}")

def playable(scene):
    return re.fullmatch(r"[a-z]\d{3}", scene) is not None

for r in body:
    note = re.sub(r"^((" + "|".join(MARKS) + r") in [^;]*" + re.escape(TAG) + r"(; )?)+", "", r[NOTE])
    marked = {m: sorted(s for (i, s), (st, _) in record.items() if i == r[ID] and st == m) for m in MARKS}
    for m in reversed(MARKS):  # note reads "fine in ...; bugged in ...; fixed in ...; <old note>"
        if marked[m]:
            note = f"{m} in {' '.join(marked[m])} {TAG}" + (f"; {note}" if note else "")
    if not r[STATUS].startswith("confirmed"):
        scenes = [s for s in r[SCENES].split() if playable(s)]
        status = "bugged" if marked["bugged"] else \
                 "fine" if scenes and all(s in marked["fine"] for s in scenes) else "suspect"
        if status != r[STATUS]:
            log.append(f"{r[ID]} {r[STATUS]} -> {status}")
            r[STATUS] = status
    r[NOTE] = note

with open(CSV, "w", newline="", encoding="utf-8") as fh:
    csv.writer(fh).writerows([head] + body)
with open(RECORD, "w", newline="", encoding="utf-8") as fh:
    w = csv.writer(fh); w.writerow(["id", "scene", "status", "date"])
    w.writerows([i, s, st, d] for (i, s), (st, d) in sorted(record.items()))
with open(MENU, "w", encoding="utf-8", newline="\n") as fh:
    fh.write("# id\tscene\tstatus  (dev menu Textures tab; tools/debug/texhunt/verdicts.py)\n")
    fh.writelines(f"{i}\t{s}\t{st}\n" for (i, s), (st, _) in sorted(record.items()))

print("\n".join(log) if log else "no changes")
print(f"record: {len(record)} per-level marks; suspects.csv: {sum(r[STATUS] == 'fine' for r in body)} fine, "
      f"{sum(r[STATUS] == 'bugged' for r in body)} bugged, "
      f"{sum(r[STATUS].startswith('confirmed') for r in body)} confirmed")
