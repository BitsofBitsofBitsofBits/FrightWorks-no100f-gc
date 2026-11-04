#!/usr/bin/env python3
"""Downloads the pinned portable build tools (llvm-mingw, CMake, Ninja) into --destination (default
buildtools/artifacts/portable-tools), checking each archive's SHA-256.

    python wiicompiled/buildtools/Prepare-PortableTools.py [--destination <folder>]

(Prepare-PortableTools.ps1 runs this on Windows.) The three packages are the Windows x86_64 builds the Windows game
build uses (llvm-mingw cross-compiles with mingw-w64), so this is Windows-only: elsewhere it stops with an error.
Python 3.8+, standard library only. Failures print "ERROR: <text>" and exit 1.
"""
import argparse
import hashlib
import os
import shutil
import sys
import time
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))

PACKAGES = [
    dict(name="llvm-mingw", file="llvm-mingw-20260616-ucrt-x86_64.zip",
         uri="https://github.com/mstorsjo/llvm-mingw/releases/download/20260616/llvm-mingw-20260616-ucrt-x86_64.zip",
         sha256="b9b68a4d276e16fa25802aaba458e4638f64b3884c290aaccdc2d87083b6ca35",
         root="llvm-mingw-20260616-ucrt-x86_64"),
    dict(name="CMake", file="cmake-4.3.3-windows-x86_64.zip",
         uri="https://github.com/Kitware/CMake/releases/download/v4.3.3/cmake-4.3.3-windows-x86_64.zip",
         sha256="935ade9e5e8723583c07f44c5592cea2a1c8f65c56ca7e07b34c025c880e0bd6",
         root="cmake-4.3.3-windows-x86_64"),
    dict(name="Ninja", file="ninja-win-1.13.2.zip",
         uri="https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip",
         sha256="07fc8261b42b20e71d1720b39068c2e14ffcee6396b76fb7a795fb460b78dc65",
         root=None),
]

LICENSES = """Portable build tools bundled by WiiCompiled

LLVM-MinGW 20260616 (LLVM/Clang/LLD, mingw-w64, libc++)
  https://github.com/mstorsjo/llvm-mingw
  Licenses are included in the llvm-mingw directory.

CMake 4.3.3
  https://cmake.org/
  License is included in the CMake directory.

Ninja 1.13.2
  https://github.com/ninja-build/ninja
  Apache License 2.0; see the installer license inventory.
"""


class Failure(Exception):
    """Stops the script (exit 1)."""


def say(text):
    print(text, flush=True)


def sha256_of(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url, path):
    request = urllib.request.Request(url, headers={"User-Agent": "FrightWorks-build-scripts"})
    with urllib.request.urlopen(request, timeout=60) as response, open(path, "wb") as out:
        shutil.copyfileobj(response, out, 1 << 20)


def remove_tree(path):
    def writable(function, target, _error):
        os.chmod(target, 0o700)
        function(target)
    if os.path.isdir(path) and not os.path.islink(path):
        shutil.rmtree(path, onerror=writable)
    elif os.path.lexists(path):
        os.remove(path)


def expand_zip(archive, destination):
    """Expand-Archive: entries keep their stored modification time."""
    with zipfile.ZipFile(archive) as bundle:
        bundle.extractall(destination)
        for info in bundle.infolist():
            if info.is_dir():
                continue
            stamp = time.mktime(info.date_time + (0, 0, -1))
            os.utime(os.path.join(destination, *info.filename.split("/")), (stamp, stamp))


def prepare(destination):
    downloads = os.path.join(HERE, "artifacts", "downloads")
    os.makedirs(destination, exist_ok=True)
    os.makedirs(downloads, exist_ok=True)

    for package in PACKAGES:
        archive = os.path.join(downloads, package["file"])
        if not os.path.isfile(archive) or sha256_of(archive) != package["sha256"]:
            say("Downloading %s..." % package["name"])
            temporary = archive + ".partial"
            if os.path.lexists(temporary):
                os.remove(temporary)
            try:
                download(package["uri"], temporary)
            except OSError as error:
                raise Failure("%s could not be downloaded: %s" % (package["name"], error))
            actual = sha256_of(temporary)
            if actual != package["sha256"]:
                raise Failure("%s hash mismatch: %s" % (package["name"], actual))
            os.replace(temporary, archive)

        target = os.path.join(destination, package["name"])
        if os.path.isdir(target):
            continue
        extract = os.path.join(destination, ".extract-" + package["name"])
        remove_tree(extract)
        expand_zip(archive, extract)
        source = os.path.join(extract, package["root"]) if package["root"] else extract
        if not os.path.isdir(source):
            raise Failure("%s archive layout changed." % package["name"])
        os.rename(source, target)
        remove_tree(extract)

    # UTF-8 with a byte order mark and CRLF line ends, as Windows PowerShell's Set-Content wrote it.
    with open(os.path.join(destination, "README-LICENSES.txt"), "wb") as out:
        out.write(b"\xef\xbb\xbf" + LICENSES.replace("\n", "\r\n").encode("utf-8"))
    say("Portable tools ready: " + destination)


def main(argv):
    parser = argparse.ArgumentParser(description="Downloads the pinned portable build tools.")
    parser.add_argument("--destination", default=os.path.join(HERE, "artifacts", "portable-tools"))
    args = parser.parse_args(argv)
    if sys.platform != "win32":
        print("ERROR: Prepare-PortableTools only fetches the Windows x86_64 build tools (llvm-mingw, CMake and Ninja "
              "zips); it has nothing to do on this system. Use your distribution's packages here.")
        return 1
    try:
        prepare(os.path.abspath(args.destination))
    except (Failure, OSError, zipfile.BadZipFile) as error:
        print("ERROR: %s" % error)
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
