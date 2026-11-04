#!/usr/bin/env python3
"""Puts the files the game ships besides the program into its folder (beside "FrightWorks Game.exe"): the built-in HD
texture pack in texture_replacements/ScoobyHD (texture_pack/ScoobyHD, kept out of git, downloaded when missing) and the
button prompt icons in button_icons (tools/make-button-icons.py). They are program files: the game indexes the player's
own UserData/texture_replacements first, and an installed game's folder is replaced on every update.

    python tools/stage-assets.py --game-dir <folder> [--required]

(tools\\stage-assets.ps1 -GameDir <folder> [-Required] runs this on Windows.) Used by build-no100f.ps1 (Play\\, after
every build) and package-scooby.ps1 (the release's data\\). Only what changed is copied or rebuilt. --required
(packaging) stops on anything missing; without it a missing piece is a warning ("WARNING: ..."), so a test build still
finishes. Failures print "ERROR: <text>" and exit 1. Right after the pack is mirrored, tools/hd-pack-fixes.py repairs
known problems in the game folder's copy (it reads the disc's textures from Disc/DATA; a skipped fix fails --required).

Python 3.8+, standard library only. The HD pack is a RAR: unpacking needs bsdtar (Windows' tar.exe, or libarchive's
bsdtar on Linux), 7-Zip or unrar. The button icons need Pillow and ImageMagick (magick), like make-button-icons.py.
"""
import argparse
import glob
import hashlib
import os
import shutil
import stat
import subprocess
import sys
import time
import urllib.request

WINDOWS = os.name == "nt"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOOLS = os.path.dirname(os.path.abspath(__file__))
PACK_URL = ("https://archive.org/download/dolphin-scooby-doo-night-of-100-frights-hd-texture-pack/"
            "Dolphin%20Scooby%20Doo%20Night%20of%20100%20Frights%20HD%20Texture%20Pack.rar")
PACK_SHA1 = "1E55E7F802BD0E5AD00538FB21D17A66417F723B"


class Failure(Exception):
    """Stops the script (exit 1)."""


def say(text):
    print(text, flush=True)


def fail(text, required):
    """A missing piece: an error when --required (packaging), else a warning."""
    if required:
        raise Failure(text)
    say("WARNING: " + text)


def is_link(path):
    """A symlink, or on Windows also a junction (any reparse point)."""
    try:
        info = os.lstat(path)
    except OSError:
        return False
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, "st_file_attributes", 0) & 0x400)


def remove_tree(path):
    def writable(function, target, _error):
        os.chmod(target, stat.S_IWRITE)
        function(target)
    if is_link(path):
        try:
            os.rmdir(path)      # a Windows directory link: removes the link, never what it points to
        except OSError:
            os.unlink(path)
    elif os.path.isdir(path):
        shutil.rmtree(path, onerror=writable)
    elif os.path.lexists(path):
        os.chmod(path, stat.S_IWRITE)
        os.remove(path)


def sha1_of(path):
    digest = hashlib.sha1()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def download(url, path, attempts=3):
    request = urllib.request.Request(url, headers={"User-Agent": "FrightWorks-build-scripts"})
    for attempt in range(1, attempts + 1):
        try:
            with urllib.request.urlopen(request, timeout=60) as response, open(path, "wb") as out:
                shutil.copyfileobj(response, out, 1 << 20)
            return
        except OSError:
            if attempt == attempts:
                raise
            time.sleep(2 * attempt)


def run_quiet(command):
    """Exit code of a command whose output isn't wanted (-1 when it can't start)."""
    try:
        return subprocess.run(command, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL).returncode
    except OSError:
        return -1


def unpack_rar(rar, unpack):
    """RAR5: libarchive's tar reads it; 7-Zip (or unrar) if that fails."""
    if WINDOWS:
        tars = [os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "tar.exe")]
    else:
        tars = [shutil.which("bsdtar")]
    for tar in tars:
        if tar and run_quiet([tar, "-xf", rar, "-C", unpack]) == 0:
            return
    candidates = [shutil.which(name) for name in ("7z", "7zz", "7za", "7zr")]
    if WINDOWS:
        candidates.append(os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "7-Zip", "7z.exe"))
    seven_zip = next((c for c in candidates if c and os.path.isfile(c)), None)
    if seven_zip:
        if run_quiet([seven_zip, "x", "-y", "-o" + unpack, rar]) != 0:
            raise Failure("7-Zip failed to unpack the RAR")
        return
    unrar = shutil.which("unrar")
    if unrar:
        if run_quiet([unrar, "x", "-y", rar, unpack + os.sep]) != 0:
            raise Failure("unrar failed to unpack the RAR")
        return
    raise Failure("can't unpack the RAR: %s failed and neither 7-Zip nor unrar is installed"
                  % ("Windows tar" if WINDOWS else "bsdtar (libarchive) is missing or"))


def download_pack(pack_source, required):
    """texture_pack/ScoobyHD = dogerki's pack, downloaded from the Internet Archive the first time."""
    rar = os.path.join(ROOT, "texture_pack", "ScoobyHD.rar")
    unpack = os.path.join(ROOT, "texture_pack", "ScoobyHD.unpack")
    try:
        os.makedirs(os.path.dirname(rar), exist_ok=True)
        if not (os.path.isfile(rar) and sha1_of(rar) == PACK_SHA1):
            say("HD pack: downloading dogerki's HD texture pack (604 MB) from archive.org...")
            partial = rar + ".partial"
            try:
                download(PACK_URL, partial)
                if sha1_of(partial) != PACK_SHA1:
                    raise Failure("the download's SHA-1 doesn't match")
                os.replace(partial, rar)
            except OSError as error:
                raise Failure("download failed (%s)" % error)
            finally:
                if os.path.exists(partial):
                    os.remove(partial)
        remove_tree(unpack)
        os.makedirs(unpack)
        unpack_rar(rar, unpack)
        # The textures sit in one game-ID folder (GIHP78).
        folders = sorted(e.path for e in os.scandir(unpack) if e.is_dir())
        folder = folders[0] if folders else None
        if not folder or not any(e.name.lower().endswith(".png") and e.is_file() for e in os.scandir(folder)):
            raise Failure("no texture folder in the RAR")
        os.rename(folder, pack_source)
        os.remove(rar)
        say("HD pack: unpacked to " + pack_source)
    except Exception as error:   # whatever went wrong, the build goes on unless --required
        fail("HD pack: %s. Download it by hand from %s and put the PNGs in %s" % (error, PACK_URL, pack_source),
             required)
    finally:
        remove_tree(unpack)


def same_file(source, target):
    """What robocopy compares: size and last write time (to 2 s, the FAT granularity)."""
    try:
        a, b = os.stat(source), os.stat(target)
    except OSError:
        return False
    return stat.S_ISREG(b.st_mode) and a.st_size == b.st_size and abs(a.st_mtime_ns - b.st_mtime_ns) < 2_000_000_000


def copy_tree(source, target, mirror):
    """robocopy source target /E (mirror=False) or /MIR (mirror=True: also removes what source doesn't have)."""
    os.makedirs(target, exist_ok=True)
    wanted = set()
    with os.scandir(source) as entries:
        for entry in sorted(entries, key=lambda e: e.name):
            wanted.add(entry.name)
            destination = os.path.join(target, entry.name)
            if entry.is_dir():
                if os.path.lexists(destination) and not os.path.isdir(destination):
                    remove_tree(destination)
                copy_tree(entry.path, destination, mirror)
            elif not same_file(entry.path, destination):
                if os.path.isdir(destination) and not is_link(destination):
                    remove_tree(destination)
                elif os.path.lexists(destination):
                    os.chmod(destination, stat.S_IWRITE | stat.S_IREAD)
                shutil.copy2(entry.path, destination)
    if mirror:
        with os.scandir(target) as entries:
            for entry in list(entries):
                if entry.name not in wanted:
                    remove_tree(entry.path)


def find_magick():
    """The directory to add to PATH for magick, '' when it's on PATH already, None when there is none."""
    if shutil.which("magick") or (not WINDOWS and shutil.which("convert")):   # Ubuntu's ImageMagick 6 has only `convert`
        return ""
    if WINDOWS:
        # ImageMagick's installer doesn't always put itself on PATH.
        for folder in sorted(glob.glob(os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"),
                                                    "ImageMagick-*")), key=os.path.basename, reverse=True):
            if os.path.isdir(folder) and os.path.isfile(os.path.join(folder, "magick.exe")):
                return folder
    return None


def stage(game_dir, required):
    os.makedirs(game_dir, exist_ok=True)

    # Builds before 2026-11 staged both into UserData, where the old copy would now win over the new one: remove them.
    # A link (the debug kit links the pack there) is only unlinked, never emptied.
    for old in (os.path.join("UserData", "texture_replacements", "ScoobyHD"), os.path.join("UserData", "button_icons")):
        path = os.path.join(game_dir, old)
        if not os.path.lexists(path):
            continue
        remove_tree(path)
        say("Removed the old copy in " + path)

    # 1. HD texture pack. texture_pack/ScoobyHD (git-ignored) = dogerki's pack plus tools/hd-pack-additions/ScoobyHD on
    # top. The game folder gets a mirror of it (other packs in texture_replacements are left alone).
    pack_source = os.path.join(ROOT, "texture_pack", "ScoobyHD")
    if not os.path.exists(pack_source):
        download_pack(pack_source, required)
    if os.path.exists(pack_source):
        try:
            copy_tree(os.path.join(TOOLS, "hd-pack-additions", "ScoobyHD"), pack_source, mirror=False)
        except OSError as error:
            raise Failure("copying the HD pack additions failed (%s)" % error)
    pack = os.path.join(game_dir, "texture_replacements", "ScoobyHD")
    if os.path.exists(pack_source):
        try:
            copy_tree(pack_source, pack, mirror=True)
        except OSError as error:
            raise Failure("copying the HD pack failed (%s)" % error)
        say("HD pack: %s (%d files)" % (pack, sum(1 for e in os.scandir(pack) if e.is_file())))
        # Fixes to dogerki's files in the game folder's copy (the mirror put the originals back; texture_pack stays as
        # downloaded). A skipped fix is a log line, or a failure with --required (packaging).
        fixes = subprocess.run([sys.executable, "-I", os.path.join(TOOLS, "hd-pack-fixes.py"), pack,
                                os.path.join(ROOT, "Disc", "DATA")] + (["--required"] if required else []),
                               stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        output = fixes.stdout.decode("utf-8", "replace").rstrip()
        if output:
            say(output)
        if fixes.returncode != 0:
            fail("HD pack fixes failed (exit code %d)" % fixes.returncode, required)
    else:
        fail("%s is missing (the built-in HD texture pack): %s not updated"
             % (os.path.join("texture_pack", "ScoobyHD"), pack), required)

    # 2. Button prompt icons, drawn from tools/button-art over the HD pack's layout. Rebuilt when the tool or the art is
    # newer than the icons (or the folder has the old layout).
    icons = os.path.join(game_dir, "button_icons")
    layout = os.path.join(icons, "layout.txt")
    tool = os.path.join(TOOLS, "make-button-icons.py")
    inputs = [tool]
    for folder, _dirs, files in os.walk(os.path.join(TOOLS, "button-art")):
        inputs.extend(os.path.join(folder, name) for name in files)
    newest = max(os.stat(path).st_mtime_ns for path in inputs)
    if os.path.isfile(layout) and os.stat(layout).st_mtime_ns >= newest:
        say("Button icons: %s (up to date)" % icons)
        return
    magick = find_magick()
    if magick is None:
        fail("button icons need Python and ImageMagick (magick, or convert on Linux) on PATH: %s not updated" % icons, required)
        return
    if not os.path.exists(pack_source):
        fail("button icons need %s: %s not updated" % (os.path.join("texture_pack", "ScoobyHD"), icons), required)
        return
    remove_tree(icons)
    env = dict(os.environ)
    if magick:
        env["PATH"] = magick + os.pathsep + env.get("PATH", "")
    # Same interpreter as this script (the one the wrapper found). Not -I: make-button-icons.py needs Pillow, which may
    # live in the user's site-packages.
    result = subprocess.run([sys.executable, tool, "--out", icons], stdin=subprocess.DEVNULL,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env)
    if result.returncode != 0:
        output = result.stdout.decode("utf-8", "replace").strip()
        raise Failure("make-button-icons.py failed" + (":\n" + output if output else ""))
    say("Button icons: %s (rebuilt)" % icons)


def main(argv):
    parser = argparse.ArgumentParser(description="Stages the HD texture pack and the button icons into a game folder.")
    parser.add_argument("--game-dir", required=True, help="the folder beside FrightWorks Game.exe")
    parser.add_argument("--required", action="store_true", help="stop on anything missing instead of warning")
    args = parser.parse_args(argv)
    try:
        stage(os.path.abspath(args.game_dir), args.required)
    except Failure as error:
        print("ERROR: %s" % error, flush=True)
        return 1
    except OSError as error:
        print("ERROR: %s" % error, flush=True)
        return 1
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as error:   # a bug or something unforeseen: say so on stdout, where the .ps1 wrapper reads it
        import traceback
        print("ERROR: unexpected %s: %s" % (type(error).__name__, error))
        traceback.print_exc(file=sys.stdout)
        sys.exit(1)
