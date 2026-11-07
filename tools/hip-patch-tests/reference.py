"""Repeats `HipPatchTests --export DISC WORK` with tools/debug/game/hipedit.py: the two must agree byte for byte.

    dotnet run --project tools/hip-patch-tests -- --export DISC WORK
    python tools/hip-patch-tests/reference.py DISC WORK

WORK holds only generated assets and hashes, but it is built from a real disc: keep it in a git-ignored folder.
"""
import hashlib
import os
import sys

disc, work = sys.argv[1:3]
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "debug", "game"))
from hipedit import Hip, xstrhash  # noqa: E402


def read(name):
    with open(os.path.join(work, name), "rb") as f:
        return f.read()


model, texture, simple = read("a.modl"), read("a.rwtx"), read("a.simp")
want = dict(line.split() for line in read("csharp.txt").decode().splitlines())

levels = {}
for folder, _, names in os.walk(os.path.join(disc, "files")):
    for name in names:
        if name.lower().endswith(".hip"):
            levels[name] = os.path.join(folder, name)

same = different = 0
for name, expected in sorted(want.items()):
    with open(levels[name], "rb") as f:
        h = Hip(f.read())
    h.add(1, b"RWTX", xstrhash("reference.RW3"), texture, "reference.RW3", 0x6, 0x40)
    h.add(3, b"MODL", xstrhash("reference"), model, "reference", 0x5, 0x10)
    h.add(0, b"SIMP", xstrhash("REFERENCE OBJ"), simple, "REFERENCE OBJ", 0x2, 0x20)
    got = hashlib.sha256(h.build()).hexdigest().upper()
    if got == expected:
        same += 1
    else:
        different += 1
        print("DIFFERENT", name, got, expected)
print(f"{same} levels the same as the launcher's code, {different} different")
sys.exit(1 if different else 0)
