#!/usr/bin/env python3
"""Upload PR / issue screenshots to FrightWorks/pr-media and print Markdown image lines.

    pr-media.py --pr N file.png [file.jpg ...]
    pr-media.py --issue N file.png ...
    pr-media.py --pr N --dry-run file.png      every check, no push
    pr-media.py --self-test                     tests against a local bare repo

Python 3.8+, standard library plus the git command. Windows and Linux.
Rules: https://github.com/FrightWorks/pr-media (screenshots only, look at each
picture first: no personal paths, names or addresses on screen).
Nothing is changed or read except the files named on the command line.
All files are checked first; one refusal means nothing is pushed.
"""
import argparse
import os
import re
import shutil
import stat
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

DEFAULT_REPO = "https://github.com/FrightWorks/pr-media.git"
RAW_BASE = "https://raw.githubusercontent.com/FrightWorks/pr-media/main"
MAX_BYTES = 10 * 1024 * 1024
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{0,63}$")
PNG_SIG = b"\x89PNG\r\n\x1a\n"
PNG_META = {b"tEXt", b"zTXt", b"iTXt", b"eXIf"}
JPEG_META = {0xE1: "APP1 (EXIF/XMP)", 0xED: "APP13 (Photoshop/IPTC)", 0xFE: "comment"}


def check_png(data):
    pos = 8
    seen_iend = False
    while pos + 8 <= len(data):
        length, ctype = struct.unpack(">I4s", data[pos:pos + 8])
        if ctype in PNG_META:
            return "PNG has a %s metadata chunk" % ctype.decode("ascii")
        pos += 12 + length
        if ctype == b"IEND":
            seen_iend = True
            break
    if not seen_iend or pos > len(data):
        return "PNG is truncated or damaged"
    if pos != len(data):
        return "PNG has %d extra bytes after its end (may hide the original image)" % (len(data) - pos)
    return None


def check_jpeg(data):
    # Limit: only the header segments before the start of scan are checked.
    # Data after the scan (including after the end marker) is not inspected.
    pos = 2
    while pos + 4 <= len(data):
        if data[pos] != 0xFF:
            return "JPEG is damaged"
        marker = data[pos + 1]
        if marker == 0xFF:
            pos += 1
            continue
        if marker in JPEG_META:
            return "JPEG has %s metadata" % JPEG_META[marker]
        if marker == 0xDA:  # start of scan: header segments are over
            return None
        if marker == 0xD9:
            return None
        if marker == 0x01 or 0xD0 <= marker <= 0xD7:
            pos += 2
            continue
        pos += 2 + struct.unpack(">H", data[pos + 2:pos + 4])[0]
    return "JPEG is truncated or damaged"


def check_file(path):
    """Return (name, data, None) or (name, None, reason)."""
    name = os.path.basename(path)
    if not NAME_RE.match(name):
        return name, None, "name must be letters, digits, . _ - only (start with a letter or digit, max 64)"
    ext = name.rsplit(".", 1)[-1].lower() if "." in name else ""
    if ext not in ("png", "jpg", "jpeg"):
        return name, None, "only .png, .jpg and .jpeg are accepted"
    try:
        size = os.path.getsize(path)
        if size > MAX_BYTES:
            return name, None, "over 10 MB (%d bytes)" % size
        with open(path, "rb") as f:
            data = f.read()
    except OSError as e:
        return name, None, "cannot read: %s" % e.strerror
    is_png = data.startswith(PNG_SIG)
    is_jpeg = data.startswith(b"\xff\xd8\xff")
    if not (is_png or is_jpeg):
        return name, None, "content is not a PNG or JPEG (wrong file signature)"
    if is_png != (ext == "png"):
        return name, None, "extension does not match the content (%s)" % ("PNG" if is_png else "JPEG")
    why = check_png(data) if is_png else check_jpeg(data)
    if why:
        return name, None, why + " (nothing is stripped: remove it and retry)"
    return name, data, None


def git(args, cwd=None, check=True):
    env = dict(os.environ, GIT_TERMINAL_PROMPT="0")
    r = subprocess.run(["git"] + args, cwd=cwd, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, universal_newlines=True)
    if check and r.returncode:
        raise RuntimeError("git %s failed:\n%s" % (" ".join(args[:2]), r.stdout.strip()))
    return r.stdout.strip()


def rmtree(path):
    def fix(func, p, _exc):
        os.chmod(p, stat.S_IWRITE)
        func(p)
    shutil.rmtree(path, onerror=fix)


def link_for(repo, rel):
    if repo == DEFAULT_REPO:
        return "%s/%s" % (RAW_BASE, rel)
    return "%s#%s" % (repo, rel)


def run(argv):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    g = ap.add_mutually_exclusive_group(required=True)
    g.add_argument("--pr", type=int, metavar="N")
    g.add_argument("--issue", type=int, metavar="N")
    ap.add_argument("--dry-run", action="store_true", help="check everything, print links, push nothing")
    ap.add_argument("--repo", default=DEFAULT_REPO, metavar="URL", help="other repository (tests)")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args(argv)
    num = a.pr if a.pr is not None else a.issue
    if num < 1:
        print("number must be positive", file=sys.stderr)
        return 2
    folder = "%s/%d" % ("pr" if a.pr is not None else "issue", num)

    files, bad, seen = [], False, set()
    for p in a.files:
        name, data, why = check_file(p)
        if not why and name in seen:
            why = "same name given twice"
        seen.add(name)
        if why:
            print("REFUSED %s: %s" % (name, why), file=sys.stderr)
            bad = True
        else:
            files.append((name, data))
    if bad:
        print("Nothing uploaded.", file=sys.stderr)
        return 1

    tmp = tempfile.mkdtemp(prefix="pr-media-")
    try:
        clone = os.path.join(tmp, "clone")
        git(["clone", "--quiet", "--depth", "1", a.repo, clone])
        dest = os.path.join(clone, *folder.split("/"))
        new = []
        for name, data in files:
            target = os.path.join(dest, name)
            if os.path.exists(target):
                with open(target, "rb") as f:
                    if f.read() != data:
                        print("REFUSED %s: %s/%s already exists with different content "
                              "(pick a new name)" % (name, folder, name), file=sys.stderr)
                        bad = True
                        continue
                print("note: %s/%s already uploaded, identical" % (folder, name), file=sys.stderr)
            else:
                new.append((name, data))
        if bad:
            print("Nothing uploaded.", file=sys.stderr)
            return 1

        if new and not a.dry_run:
            os.makedirs(dest, exist_ok=True)
            for name, data in new:
                with open(os.path.join(dest, name), "wb") as f:
                    f.write(data)
            if not git(["config", "user.name"], cwd=clone, check=False) or \
                    not git(["config", "user.email"], cwd=clone, check=False):
                print("No git identity configured (user.name / user.email).", file=sys.stderr)
                return 1
            git(["add", "--"] + ["%s/%s" % (folder, n) for n, _ in new], cwd=clone)
            git(["commit", "--quiet", "-m", "Add %s media (%d file%s)" % (folder, len(new), "" if len(new) == 1 else "s")],
                cwd=clone)
            branch = git(["rev-parse", "--abbrev-ref", "HEAD"], cwd=clone)
            try:
                git(["push", "--quiet", "origin", "HEAD:refs/heads/" + branch], cwd=clone)
            except RuntimeError as e:
                print(e, file=sys.stderr)
                print("Push failed; not retried. Command was: git push origin HEAD:refs/heads/%s "
                      "(in a shallow clone of %s)" % (branch, a.repo), file=sys.stderr)
                return 1

        print("%s %s/ in %s" % ("Would put" if a.dry_run else "Uploaded to", folder, a.repo), file=sys.stderr)
        for name, _ in files:
            print("![describe this picture](%s)" % link_for(a.repo, "%s/%s" % (folder, name)))
        return 0
    finally:
        rmtree(tmp)


# ---------------------------------------------------------------- self-test

def _chunk(t, body):
    import zlib
    return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)


def _png(extra=b"", pad=0):
    import zlib
    ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    idat = zlib.compress(b"\x00\xff\x00\x00")
    out = PNG_SIG + _chunk(b"IHDR", ihdr) + extra + _chunk(b"IDAT", idat)
    if pad:
        out += _chunk(b"prVt", b"\0" * pad)  # private ancillary chunk, only for size
    return out + _chunk(b"IEND", b"")


def self_test():
    os.environ.setdefault("GIT_AUTHOR_NAME", "selftest")
    os.environ.setdefault("GIT_AUTHOR_EMAIL", "selftest@example.invalid")
    os.environ.setdefault("GIT_COMMITTER_NAME", os.environ["GIT_AUTHOR_NAME"])
    os.environ.setdefault("GIT_COMMITTER_EMAIL", os.environ["GIT_AUTHOR_EMAIL"])
    tmp = tempfile.mkdtemp(prefix="pr-media-test-")
    results = []
    try:
        bare = os.path.join(tmp, "bare.git")
        git(["init", "--quiet", "--bare", bare])
        git(["symbolic-ref", "HEAD", "refs/heads/main"], cwd=bare)
        seed = os.path.join(tmp, "seed")
        git(["init", "--quiet", seed])
        git(["checkout", "--quiet", "-b", "main"], cwd=seed)
        Path(seed, "README.md").write_text("seed\n")
        git(["add", "."], cwd=seed)
        git(["commit", "--quiet", "-m", "seed"], cwd=seed)
        git(["push", "--quiet", bare, "main"], cwd=seed)
        repo = Path(bare).resolve().as_uri()

        d = os.path.join(tmp, "in")
        os.makedirs(d)

        def w(name, data, sub=""):
            os.makedirs(os.path.join(d, sub), exist_ok=True)
            p = os.path.join(d, sub, name)
            with open(p, "wb") as f:
                f.write(data)
            return p

        good = _png()
        cases = [
            ("good png uploads", ["--pr", "7", w("good.png", good)], 0),
            ("wrong signature refused", ["--pr", "7", w("fake.png", b"GIF89a" + b"\0" * 20)], 1),
            ("over 10 MB refused", ["--pr", "7", w("big.png", _png(pad=MAX_BYTES))], 1),
            ("png with text chunk refused", ["--pr", "7", w("meta.png", _png(_chunk(b"tEXt", b"Author\0me")))], 1),
            ("identical re-upload ok", ["--pr", "7", w("good.png", good)], 0),
            ("same name, different bytes refused", ["--pr", "7", w("good.png", sub="other", data=_png(_chunk(b"gAMA", b"\0\0\xb1\x8f")))], 1),
            ("png with trailing bytes refused", ["--pr", "7", w("tail.png", good + b"extra")], 1),
            ("jpeg with EXIF refused", ["--issue", "9", w("e.jpg", b"\xff\xd8\xff\xe1\x00\x08Exif\0\0\xff\xd9")], 1),
            ("plain jpeg dry-run ok", ["--issue", "9", "--dry-run", w("p.jpg", b"\xff\xd8\xff\xdb\x00\x04\0\0\xff\xda\x00\x02\xff\xd9")], 0),
        ]
        for label, args, want in cases:
            got = run(args[:2] + ["--repo", repo] + args[2:])
            results.append((label, got == want))
        check = os.path.join(tmp, "check")
        git(["clone", "--quiet", repo, check])
        tree = sorted(p.relative_to(check).as_posix() for p in Path(check).rglob("*.*")
                      if ".git" not in p.parts)
        results.append(("repo holds only seed + pr/7/good.png", tree == ["README.md", "pr/7/good.png"]))
        log = git(["log", "--oneline"], cwd=check).count("\n") + 1
        results.append(("two commits (seed + one upload)", log == 2))
    finally:
        rmtree(tmp)
    ok = True
    print("\nself-test results:")
    for label, passed in results:
        print("  %s  %s" % ("PASS" if passed else "FAIL", label))
        ok = ok and passed
    return 0 if ok else 1


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    sys.exit(run(sys.argv[1:]))
