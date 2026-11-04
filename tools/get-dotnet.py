#!/usr/bin/env python3
"""Prints the path of a dotnet with the .NET 10 SDK: the portable one in toolchain/dotnet, installed there on first use
with Microsoft's dotnet-install script (nothing is installed system-wide). A toolchain/dotnet from before the move to
.NET 10 gets the new SDK beside its old one. The last line of the output is the path.

    python tools/get-dotnet.py          (tools\\get-dotnet.ps1 runs this on Windows)

Python 3.8+, standard library only. Failures print "ERROR: <text>" and exit 1.
"""
import os
import re
import subprocess
import sys
import urllib.request

WINDOWS = os.name == "nt"
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DIR = os.path.join(ROOT, "toolchain", "dotnet")
DOTNET = os.path.join(DIR, "dotnet.exe" if WINDOWS else "dotnet")
# No telemetry anywhere in this project: the dotnet commands below must not send any.
ENV = dict(os.environ, DOTNET_CLI_TELEMETRY_OPTOUT="1")


def has_sdk10():
    if not os.path.isfile(DOTNET):
        return False
    try:
        result = subprocess.run([DOTNET, "--list-sdks"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, env=ENV)
    except OSError:
        return False
    return any(re.match(r"^10\.", line) for line in result.stdout.decode("utf-8", "replace").splitlines())


def download(url, path):
    request = urllib.request.Request(url, headers={"User-Agent": "FrightWorks-build-scripts"})
    temporary = path + ".partial"
    with urllib.request.urlopen(request, timeout=60) as response, open(temporary, "wb") as out:
        while True:
            chunk = response.read(1 << 20)
            if not chunk:
                break
            out.write(chunk)
    os.replace(temporary, path)


def main():
    if not has_sdk10():
        os.makedirs(DIR, exist_ok=True)
        # The install replaces dotnet.exe: build servers left running from this folder would lock it.
        if os.path.exists(DOTNET):
            try:
                subprocess.run([DOTNET, "build-server", "shutdown"], stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL, env=ENV)
            except OSError:
                pass
        installer = os.path.join(DIR, "dotnet-install.ps1" if WINDOWS else "dotnet-install.sh")
        print("Downloading the .NET 10 SDK into %s ..." % DIR, flush=True)
        try:
            download("https://dot.net/v1/" + os.path.basename(installer), installer)
        except OSError as error:
            print("ERROR: downloading %s failed: %s" % (os.path.basename(installer), error))
            return 1
        if WINDOWS:
            powershell = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "WindowsPowerShell",
                                      "v1.0", "powershell.exe")
            # Process-scoped bypass: the downloaded script is not signed; no policy is changed.
            command = [powershell, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", installer,
                       "-Channel", "10.0", "-InstallDir", DIR, "-NoPath"]
        else:
            command = ["bash", installer, "--channel", "10.0", "--install-dir", DIR, "--no-path"]
        try:
            process = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, env=ENV)
            for raw in process.stdout:
                print(raw.decode("utf-8", "replace").rstrip("\r\n"), flush=True)
            process.wait()
        except OSError as error:
            print("ERROR: running %s failed: %s" % (os.path.basename(installer), error))
            return 1
        if not has_sdk10():
            print("ERROR: .NET 10 SDK install failed (%s)" % DIR)
            return 1
    print(DOTNET)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except Exception as error:   # a bug or something unforeseen: say so on stdout, where the .ps1 wrapper reads it
        import traceback
        print("ERROR: unexpected %s: %s" % (type(error).__name__, error))
        traceback.print_exc(file=sys.stdout)
        sys.exit(1)
