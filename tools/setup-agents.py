#!/usr/bin/env python3
"""Makes .claude/skills a local link to the tracked .agents/skills, so Claude Code finds the repo's skills.

    python tools/setup-agents.py            (Windows: py -3 tools\\setup-agents.py)

The skills live in .agents/skills (the tool-neutral place, tracked). Claude Code only looks in .claude/skills, and
/.claude/ is git-ignored, so each checkout (the main one and every worktree) links its own .claude/skills to its own
.agents/skills: a junction on Windows (needs no admin rights or Developer Mode), a relative symlink elsewhere.

Safe to run again: it changes nothing when the link is already right. It never deletes anything but a link or an
empty folder:
  - nothing there                   -> link made
  - a link to .agents/skills        -> left alone
  - a dangling link                 -> replaced (only the link is removed)
  - a link to somewhere else        -> refused; --relink replaces it (only the link is removed)
  - a folder with only empty folders-> those empty folders are removed, then the link is made
  - a folder with any file or link  -> refused: move or delete it yourself (an old checkout's .claude/skills that git
                                       has not cleaned up yet, or your own skills)
  - a file                          -> refused

Python 3.8+, standard library only. Exit 0 on success, 1 when it refused or failed (the reason is printed).
"""
import argparse
import os
import stat
import subprocess
import sys

WINDOWS = os.name == "nt"
FILE_ATTRIBUTE_DIRECTORY = 0x10
FILE_ATTRIBUTE_REPARSE_POINT = 0x400

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, ".agents", "skills")
LINK = os.path.join(ROOT, ".claude", "skills")


class Refuse(Exception):
    pass


def is_link(path):
    """A symlink, junction or other reparse point; never follows it."""
    st = os.lstat(path)
    if WINDOWS:
        return bool(getattr(st, "st_file_attributes", 0) & FILE_ATTRIBUTE_REPARSE_POINT)
    return stat.S_ISLNK(st.st_mode)


def remove_link(path):
    """Removes the link itself, never what it points to."""
    if not is_link(path):
        raise Refuse("internal error: %s is not a link" % path)
    st = os.lstat(path)
    if WINDOWS and getattr(st, "st_file_attributes", 0) & FILE_ATTRIBUTE_DIRECTORY:
        os.rmdir(path)  # RemoveDirectory on a junction or directory symlink removes the link only
    else:
        os.unlink(path)


def points_to_source(path):
    try:
        return os.path.samefile(path, SOURCE)
    except OSError:
        return False  # dangling, or not readable


def empty_tree(path):
    """The directories under path (deepest first) if it holds no file and no link at any depth, else None."""
    found = []
    stack = [path]
    while stack:
        current = stack.pop()
        found.append(current)
        with os.scandir(current) as it:
            for entry in it:
                st = entry.stat(follow_symlinks=False)
                if WINDOWS:
                    attrs = getattr(st, "st_file_attributes", 0)
                    plain_dir = bool(attrs & FILE_ATTRIBUTE_DIRECTORY) and not attrs & FILE_ATTRIBUTE_REPARSE_POINT
                else:
                    plain_dir = stat.S_ISDIR(st.st_mode)
                if not plain_dir:
                    return None
                stack.append(entry.path)
    return sorted(found, key=len, reverse=True)


def make_link():
    os.makedirs(os.path.dirname(LINK), exist_ok=True)
    if WINDOWS:
        try:
            import _winapi
            _winapi.CreateJunction(SOURCE, LINK)
            return
        except (ImportError, AttributeError, OSError):
            pass
        result = subprocess.run(["cmd", "/c", "mklink", "/J", LINK, SOURCE],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode != 0:
            raise Refuse("could not create the junction %s -> %s" % (LINK, SOURCE))
    else:
        os.symlink(os.path.relpath(SOURCE, os.path.dirname(LINK)), LINK, target_is_directory=True)


def run(relink):
    if not os.path.isdir(SOURCE):
        raise Refuse("%s is missing. Is this checkout from before the skills moved to .agents/skills? "
                     "Update the branch first." % SOURCE)
    if is_link(SOURCE):
        raise Refuse("%s is itself a link; refusing" % SOURCE)

    if not os.path.lexists(LINK):
        make_link()
        return "linked %s -> %s" % (LINK, SOURCE)

    if is_link(LINK):
        if points_to_source(LINK):
            return "already set up: %s -> %s" % (LINK, SOURCE)
        if os.path.exists(LINK) and not relink:
            raise Refuse("%s is a link to somewhere else (%s). Run again with --relink to replace the link; "
                         "its target is not touched." % (LINK, os.path.realpath(LINK)))
        remove_link(LINK)
        make_link()
        return "replaced the link %s, now -> %s" % (LINK, SOURCE)

    if not os.path.isdir(LINK):
        raise Refuse("%s is a file; move it away and run again" % LINK)

    empties = empty_tree(LINK)
    if empties is None:
        raise Refuse("%s is a real folder with files in it; not replacing it. If it is an old copy of the skills, "
                     "delete it yourself (git rm has usually done that after updating the branch) and run again. "
                     "Skills of your own belong in .agents/skills." % LINK)
    for directory in empties:
        os.rmdir(directory)
    make_link()
    return "linked %s -> %s (an empty folder was in the way)" % (LINK, SOURCE)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Link .claude/skills to .agents/skills (local, git-ignored).")
    parser.add_argument("--relink", action="store_true", help="replace a link that points somewhere else")
    args = parser.parse_args(argv)
    try:
        print("setup-agents: " + run(args.relink))
    except Refuse as error:
        print("setup-agents: " + str(error), file=sys.stderr)
        return 1
    except OSError as error:
        print("setup-agents: failed: %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
