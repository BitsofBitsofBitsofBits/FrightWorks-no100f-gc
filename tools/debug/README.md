# Debug kit

Tools and Claude Code skills for finding bugs in the FrightWorks static recomp. New here? Start with the walkthrough in
[`docs/testing.md`](../../docs/testing.md); this page is the reference. Only things that helped in a real
investigation (upstream PRs #8, #10–#15, #22, issue #24) or will help the next one. The build tools (`tools\build-no100f.ps1`,
`shot-pid.ps1`, ...) live one folder up; nothing here contains game data.

Until 2026-10-31 this kit was a separate repo (Scooby-Doo-Refrightened-debug) linked in with junctions. Its history was
merged in with the paths rewritten, so `git log -- tools/debug .claude/skills` shows it.

Layout (paths in the skills and scripts are written from the repo root):

| Path | What |
| --- | --- |
| `.claude\skills\` | the Claude Code skills (tracked; the rest of `.claude\` is git-ignored) |
| `tools\debug\` | these tools |
| `tools\debug\local\` | git-ignored, machine-local: `user-setup\` (the user's Config.toml + controller files while a test runs; `manual-session.ps1 restore` puts them back), Dolphin disc copies, a downloaded ffmpeg, `kenney-input-prompts\` (the whole Kenney Input Prompts pack, CC0, from kenney.nl: pick new button art there and copy only the SVGs used into `tools\button-art\<family>\`) |
| `autotest\` | git-ignored work data and output: shots (`autotest\manual\`), texture-hunt data (`autotest\texhunt\`), audio dumps, logs |

## Skills

| Skill | Use for |
| --- | --- |
| `hle-divergence-debug` | Geometry/state/GPU bugs: guest data integrity, native override vs original, Dolphin DFF oracle. Cases: PR #8 (+ ghost regression), #10, #12/#13 |
| `game-logic-debug` | Frame-rate (fps60) and gameplay bugs: fixed spawn spots, F8 dev menu (any scene, saved spots, god mode, monsters off, power-ups on/off, free camera, Textures tab), live memory (postool), particle pool/emitter counts, flicker measurement. Case: PR #14 |
| `manual-yn-bisect` | Locked landing + in-game Y/N toast for human visual checks; fix-only PR workflow (`strip-before-pr.md`) |
| `audio-debug` | Crackle/clicks/fuzz/stuck loops: capture, per-stage dumps, disc reference decode. Case: PR #11 |
| `parallel-work` | Several tasks at once: worktrees and their Claude sessions, started, checked on, messaged and removed by the main checkout's session (`worktree.ps1`) |

## Tools

| Path | What |
| --- | --- |
| `play-patched.ps1` | Apply patches, build, play with the dev menu on (`NO100F_DEV_TOOLS=1`; the default `devmenu` patch only on branches from before it was built in); patches come out of the source when the game closes. `-Patch a,b`, `-HdTextures` (with the HD pack), `-Keep`, `-CleanBuild`, `-Remove` |
| `manual-session.ps1` | Single-window playtest: `start -Boot <level> [-AllPowerups]` (saves the user's settings, writes the test Config + `zz-manual` boot mod), `shot`, `stop` (settings back), `prepare` (like start, no game), `restore`, `stop-tests`. Stays inside its own checkout; `prepare`/`start` wait for the game-run lock |
| `worktree.ps1` | Parallel work (the `parallel-work` skill): `new -Name <n> -Branch <b> [-From origin/main] [-Task "goal"] [-Build] [-NoOpen]` (a git worktree in `..\FrightWorks-worktrees\<n>` with its own Play\, links to the main checkout's Disc\, texture_pack\, toolchain\, Dependencies and `local\` folders, and a Windows Terminal tab running `claude -n <n>`; `-Task` writes the goal into its TASK.md and the session starts on it), `list`, `open -Name <n> [-Continue]` (a new session that picks the task up from TASK.md, or `claude -c`; `-Name main` = the main checkout's coordinating session, named `coordinator`; `new`/`open` drop the Claude Code variables (`CLAUDECODE`, `CLAUDE_PID`, `AI_AGENT`, `CLAUDE_CODE_*`) of the calling shell, so the tab's session is its own, not a child of the coordinator: ListAgents sees it and `-Continue` can resume it), `remove -Name <n>` (stops leftover dotnet build servers; refuses with uncommitted work or a running program; unlinks before deleting; keeps the branch) |
| `kit-common.ps1` | Dot-sourced by the scripts: `Get-/Stop-CheckoutGames <root>` (only games started from that checkout) and the game-run lock (`Enter-/Exit-GameLock`, `%LOCALAPPDATA%\FrightWorks-kit\game-run.lock`): one checkout runs the game for a test at a time, builds run in parallel, the user's own game is never waited for or closed |
| `build-translator.ps1` | Rebuild the translator after switching to/from a branch that changes `wiicompiled\translator` (then build without `-SkipTranslate`) |
| `dol.py` | `callers` / `func` / `dis` / `name` on `game/main.dol` with MAP names (needs `capstone`) |
| `overrides.py` | Active native overrides; `--effects`, `--xref` (`__GXData` fields only the replaced original maintained) |
| `dff/` | Dolphin `.dff` FIFO log decoder, renders, pool/dynamic-array listers |
| `audio/` | `crackle.py` click detector, `adpcm_ref.py` disc reference decode + bit-exact compare, `hip.py` HIP asset lister, `loopcheck.py` (is the output stuck repeating a short loop? `--scan W` over a long dump), `loop-capture.ps1` (boot a scene with the audio dump on, then `loopcheck.py`) |
| `game/` | `levels.md` (scene IDs ↔ level names, which scenes to test what in) + `levels.py` (reads the table from `main.dol`), `mkhip.py` (spawn spot / move NPCs via the boot mod), `spots.txt`, `findpos.py`, `addrrefs.py`, `parsys.py`, `births.py` + `births-run.ps1`, `fogdump.ps1` + `fogsum.py` (i004 fog bank state from a running dev menu build), `fanfog-walk.py` (walk one particle list from a pool peek) |
| `texhunt/` | HD texture work, see below |
| `capture/` | `get-ffmpeg.ps1` + `ffbin.py` (find or fetch ffmpeg), `record.ps1` (video of the game window: ddagrab + topmost by default, `-Method gdigrab`), `flashcount.py` (one-frame flash counter), `triplet.py` |
| `yn-bisect/` | `curtain-repro.ps1` scripted landing template, `curtain-ask.ps1` Y/N toast helper (need `yn-bisect-harness.patch`) |
| `holiday-shots.ps1` | Screenshot loop for `[game] holiday` dates per hub (needs PR #13) |
| `patches/` | Diagnostics kept out of PRs; table in `.claude\skills\manual-yn-bisect\strip-before-pr.md` |

### `texhunt/` (HD texture packs, cut-outs, halos)

Data and output go to `autotest\texhunt\` (`suspects.csv`, `devmenu_textures.txt`, `disc.pkl`, packs, shots); the
session scripts need the dev menu build in `Play\` (`build-devmenu.ps1` or `play-patched.ps1`) and restore the user's
setup when the game closes.

| Tool | What |
| --- | --- |
| `look.ps1 -Boot e005 -Spot <spot> [-Hd \| -Pack <dir>] [-Extra <dir>] [-Interp N] [-KeepIntro]` | Open the game at a saved dev-menu spot (intro cutscene skipped) and leave it to the human |
| `hunt.ps1 [-Boot h001] [-Pack <dir>] [-Branch b] [-TexLog]` | Texture-hunt session: dev menu + HD pack, every powerup, no hub intro, audio dump checked for a stuck loop |
| `spots-run.ps1 -Boot h001 -Ids a,b -Mode orig\|hd\|clean [-Tag x] [-Branch b]` | Screenshot each saved spot |
| `cutout-shots.ps1`, `perf-cutout.ps1` | Shots / draw-call + frame-time numbers per cut-out mode (needs aurora's cut-out modes) |
| `build-devmenu.ps1 -Branch b` | `Play\` = that branch with the dev menu (the patch on older branches), source put back afterwards |
| `gxtex.py` | XXH64 + RWTX reader + GX decoders; names textures like the port (`tex1_WxH_<hash>_<fmt>`) |
| `where.py [IDs]` | Which assets use a suspect's texture; BSP geometry gets `tp X Y Z` positions |
| `tint-pack.py --out <dir> (--from <dir> \| --hip <HIP>) [--match s]` | Tinted copies of textures; link them with `look.ps1 -Extra` to see where a texture is drawn |
| `creeper-frames.py` | The HD Creeper window frames (issue #24) from the disc originals + ScoobyHD's window (needs SciPy) |
| `skipintro.py <mod dir>` | h001.HIP without the intro cutscene (written into a boot mod, never committed) |
| `ogexport.py`, `clean.py`, `onoff.py`, `verdicts.py` | Original-vs-HD sheets (local only), cleaned and on/off test packs, dev menu marks ↔ `suspects.csv` |

Patches are against upstream `main` `e600261` unless the table says otherwise. `gx-draw-probes.patch` was rebased onto
PR #8 `622b97a` and not rebuilt since.

Requirements: Python 3 with numpy + Pillow (+ `capstone` for `dol.py`/`overrides.py`, SciPy for `creeper-frames.py`). The
`capture\` tools use ffmpeg from PATH; without one, `capture\get-ffmpeg.ps1` downloads the BtbN win64 GPL build once into
`tools\debug\local\ffmpeg` (SHA-256 checked).
