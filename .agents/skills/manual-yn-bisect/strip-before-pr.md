# Fix-only PR branch (keep diagnostics as patches)

Diagnostics never ride along in a PR. Keep them reusable as patch files instead of stashes or WIP branches.

## Workflow (used for PR #8)

1. Save diagnosis code as patches against the current base, split by purpose:
   ```powershell
   git diff --output=tools/debug/patches/<name>.patch HEAD -- <files>   # byte-exact; don't use Out-File
   git apply --check tools/debug/patches/<name>.patch
   ```
2. Fresh branch from `origin/main`, apply **only** the fix, rebuild, re-verify: a human visual check if the bug is visible, and the smoke test in `.agents/REPO.md` (Testing; the script is not built yet, so boot `h001` and the changed level).
3. Commit only the fix paths: `git commit -m "..." -- <fix files>`.
4. Check overlap with open PRs: `gh pr view <n> --json files`, then
   `git fetch origin pull/<n>/head:refs/remotes/origin/pr-<n>` and `git merge-tree --write-tree HEAD origin/pr-<n>`.

Existing patches in `tools/debug/patches/` (apply with `git apply`, remove with `git apply -R`). Base `e600261` (`main`)
unless noted; if one stops applying after `main` moves, use `git apply -3`, fix conflicts and re-save it.

| Patch | Contents | Skill |
| --- | --- | --- |
| `dltail-diagnostic` | `NO100F_DLTAIL` display-list build-vs-call integrity check | `hle-divergence-debug` |
| `gx-draw-probes` | Per-list paint/IDVIEW, `[livevtx]`, `[shadowvcd]`, state checks, DL A/B switches. Base PR #8 `622b97a`; pads `GXEndDisplayList` unless `NO100F_DL_NOPAD=1` | `hle-divergence-debug` |
| `clear-magenta-diagnostic` | `NO100F_CLEAR_MAGENTA` EFB clear colour (holes vs coloured pixels) | `hle-divergence-debug` |
| `fogstat-probes` | While `Play/fogstat.txt` exists: per-frame blended draw counts/coverage, guest backtrace per raw draw, i004 fog quads | `game-logic-debug` |
| `postool` | `Play/postool.txt` commands (dump/peek/poke/trace/births) + F9 spot save | `game-logic-debug` |
| `devmenu` | F8 dev menu: go to any scene, saved spots, god mode, monsters off, `Play/devmenu.txt` commands. New `no100f_devmenu.*` + hooks in `settings_overlay.cpp`; also applies to `release/1.0.3-beta`. `tools/play-patched.ps1` applies it for one play session | `game-logic-debug` |
| `audio-diagnostic` | `AX_DIAG_DIR` per-stage audio dumps, `AX_HEADROOM_DB` | `audio-debug` |
| `real-date` | `NO100F_REAL_DATE=1` real clock for builds without PR #12 | — |
| `pad-script` | `NO100F_PAD_SCRIPT` / `NO100F_PAD_SCRIPT_LIVE` scripted pad 1 (stick and buttons at set times, dev tools on or off). Adds `no100f_pad_script.h`; the format is at its top. Base `ee96a38` | `game-logic-debug` |
| `yn-bisect-harness` | Toast + Y/N keys, `STICK_*` input script + `curtain-stick.txt`, GPU owner dump, event skip / flat-paint files, old curtain tracers and env skips | this skill |

## Never in a PR

- Bisect-only code (owner meta, skip/flat files, tracers, `NO100F_*` diagnostics, toast/Y/N unless the maintainer wants it)
- Local ignore rules: they live in `.git/info/exclude`, not `.gitignore`
- ISO / `Disc/` / `autotest/` / `Play/` / `AGENTS.md` / `tools/debug/` / `.agents/skills/` (the debug kit: committed on its own branch, never in a fix PR)

## Verify after strip

```powershell
Remove-Item Env:NO100F_* -ErrorAction SilentlyContinue
powershell -ExecutionPolicy Bypass -File tools\build-no100f.ps1 -SkipTools -SkipTranslate
powershell -ExecutionPolicy Bypass -File tools\debug\manual-session.ps1 start -Boot <LEVEL>
# human visual if the bug is visible: bug gone. Then the smoke test (.agents/REPO.md, Testing)
```
