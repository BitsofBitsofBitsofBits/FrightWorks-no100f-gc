# FrightWorks: shared notes for agents

Rules and facts every agent working on this repo needs, whichever tool it runs in. This file is public. People and agents also keep their own git-ignored `AGENTS.md` at the repo root for anything specific to their machine: put a line there pointing here.

## Using this file

- **Read it before changing anything.** The skills in `.agents/skills/` go deeper: read the matching one first. Claude Code looks for skills in `.claude/skills/` (git-ignored): run `python tools/setup-agents.py` once per checkout to link it to `.agents/skills/` (`worktree.ps1 new` does that for new worktrees).
- **Change it in the PR that makes the change true.** A new rule or fact ships in the same diff as the work behind it. Standalone corrections are fine as their own small PR.
- **Rules and lasting facts only.** Who is working on what belongs in GitHub issues and PRs. Case histories belong in the skills. How to build and test belongs in `docs/testing.md`.
- **It's public, and agents act on it.** No personal paths, machine details, credentials, logs or game data. Review edits to `.agents/` as carefully as code.

## The project

GameCube port of Scooby-Doo! Night of 100 Frights: a static recompilation built on WiiCompiled + Aurora. GPLv3 (from WiiCompiled), which can't be relicensed. Split from Scooby-Doo: Refrightened by JosiahG at v1.0.2. The goal is a clean remaster, not matching Dolphin: Dolphin and the disc's data show what the game intends.

| Path | What |
| --- | --- |
| `wiicompiled/translator` | C#: `main.dol` to C++ |
| `wiicompiled/runtime` | Native runtime and game overrides (C++) |
| `wiicompiled/aurora-main` | Graphics, window, input (SDL3, WebGPU through Dawn) |
| `launcher/` | Launcher (C#, Avalonia 12, Windows and Linux) |
| `tools/`, `tools/debug/` | Build scripts; debug kit, listed in `tools/debug/README.md` |
| `docs/testing.md` | How to build, test and debug |
| `Disc/`, `Play/`, `autotest/`, `tools/debug/local/` | Git-ignored: disc files, build output, work data, machine-local files |

## Rules

**Never commit**
- A disc image, `Disc/`, or anything extracted or dumped from the game.
- The HD pack. A pack texture that needs fixing is edited by a script on every build (`tools/hd-pack-fixes.py`), with no pack image data in git. The Creeper frames in `tools/hd-pack-additions` are the only exception.
- Root `AGENTS.md` or `CLAUDE.md`, logs, recordings, or personal paths.

**Pull requests**
- Everything reaches `main` through a PR, small docs changes too. Rulesets have no bypass, even for admins. Never force-push. `archive*` branches and tags are read only and can't be moved or deleted once created.
- Fix-only PRs branch off `main` with minimal diffs and no heuristics that hide symptoms. Diagnostics never go in a fix PR (patches live in `tools/debug/patches/`). The debug kit changes in its own PRs.
- Open as a draft. Say plainly what was and wasn't tested.
- PR bodies mention other PRs only if they were tested together, with conflict status. No tool-generated footer; commits carry the co-author line.
- Before editing a PR or issue body, fetch the current one and edit that: someone may have added media.
- `gh` can't upload media. Add `<!-- drop <file> here -->` markers and collect the files in one folder for a person to drag in. GitHub won't attach `.wav` or `.mp3`: use `.mp4`.
- Keep contributors as commit authors when moving their work.
- Issue labels: at most one per category (`type:`, `area:`, `priority:`, `status:`). No GitHub issue types.

**Docs**
- README is for players and builders only. Implementation notes go in code comments and PR bodies, build tips in `docs/testing.md` (keep it in step when tools change), player-visible fixes in release notes.

**No telemetry anywhere:** builds, tools, launcher, game, dependencies. Check new dependencies for it: Avalonia's `Avalonia.BuildServices` posts on every compile, so it's pinned and blocked through the root `Directory.Build.targets` (bumping Avalonia may need that pin bumped). Set `DOTNET_CLI_TELEMETRY_OPTOUT=1` when running `dotnet` by hand.

**Test runs**
- Leave the user's setup as found: `manual-session` `prepare`/`start` save `Config.toml` and the `*.controller` files, `stop`/`restore` put them back.
- Rumble off (`[controller] rumble = false`), always. The game's default is on, so check before any launch.
- Sound muted (`[audio] muted = true`), unless a person is testing or playing it or the task is audio: then on at 5% (`[audio] volume = 0.05`). Muting only affects output: `AUDIO_RAW_DUMP` captures are unaffected.
- Don't use `level-sweep.py` or several windows for visual bugs: one window, looked at by a person.
- No on-screen sweeps through launcher or menu options. Compare options in code or headless, and sample one or two of each kind on screen.
- The game reads the controller without window focus (#123), so a test game can act on a pad someone is using. Check that nobody's game is running before a run on a shared desktop.

**Credits:** in game "Made by Loneranger419 with the help of BitsofBitsofBits" (`no100f_credits.h`; later helpers are appended to `kHelpers`). JosiahG in the README, launcher Extras > About and `tools/package-README.txt`. ScoobyHD by dogerki in the README.

## Testing

Keep game launches short and critical. Test code without launching the game where that can work.

| Change | Run |
| --- | --- |
| Docs | Nothing |
| Launcher | Layout tests; a person checks it only if it looks different |
| Translator | Translator tests + smoke test |
| Game runtime or overrides | Runtime tests + smoke test; a person checks it only if behaviour changes |
| Graphics (Aurora) | Aurora tests + smoke test + a person checks it |
| Audio | Runtime tests + smoke test + a person listens |

The commands for the runtime tests (CTest) and the launcher layout tests are in `docs/testing.md`. The Translator tests are the xUnit project `wiicompiled/translator/tests/Translator.Tests` and Aurora's are its standalone build in `wiicompiled/aurora-main/tests`: neither has a documented command yet. Anything beyond this needs a stated reason.

**Not built yet:** the smoke test script, shared run slots and the full sweep before a release. Until they exist, a smoke test means booting `h001` and the changed level with `manual-session` from a fresh build, checking the build ID, the level load and that the game stays up. Nothing runs these automatically: there is no CI, so the PR says what was run.

## Working in parallel

One task per git worktree, each with its own session: `tools/debug/worktree.ps1`, steps in the `parallel-work` skill.

In a worktree, `Disc`, `texture_pack`, `toolchain`, `game` and `wiicompiled/Dependencies` are links to the main checkout: read only. Never delete a worktree folder by hand; `remove` unlinks first. A worktree's first build must be full. Never name a session `main`.

## Facts worth knowing

**Build**
- Adding or removing overrides, switching between branches that differ in them, or building after a patch build needs a full translate (no `-SkipTranslate`), or the game stops at boot with "Fatal Error", PC=0.
- `build-no100f.ps1` builds the translator only if `bin\Release\net10.0\Translator.Cli.dll` is missing. Delete `bin\Release` after translator edits, or the output comes from a stale translator.
- An open launcher locks `Play\SDL3.dll`: the build fails at the DLL copy after the exe copy, which is harmless.
- Kit scripts must parse in PowerShell 5.1. `powershell -File x.ps1 -List a,b` passes one string, so split it in the script. A bad `BOOT=` crashes the game. New cross-platform scripts are Python 3.8+.
- Build ID (`runtime/cmake/build_info.cmake`): `<Version> branch@commit[+hash of uncommitted diff]`, shown in `console.log`, F10 About, the DEV mark and the launcher footer.

**Dev tools and input**
- `[game] dev_tools` or `NO100F_DEV_TOOLS=1`: F8 dev menu (`no100f_devmenu.cpp`), F3 recording, DEV mark.
- Scripted presses: `Play/devmenu_press.txt` (dev tools only) or `NSMBW_INPUT_SCRIPT`. SendInput keys don't reach the game. Keyboard pad input needs window focus.
- Xbox layout swaps B and X in gameplay only (menus are modes 2, 4, 6, 7 and scene H003). If a button acts oddly, check `[controller]` in `Config.toml` first.
- F10 reads the physical pad, not `[controller]`, so a bad remap can't lock anyone out.
- Game state for UI: `no100f_game_state.h`. Native overrides needing guest interrupts off: `OS__DisableInterrupts_801a65ac` / `OS__RestoreInterrupts_801a65d4`.

**Levels and saves**
- Scene IDs and which scenes suit which tests: `tools/debug/game/levels.md`. Boot `i004` directly (`i003` is snack-gated). `h001` can stick on its intro.
- The save object is the spinning Mystery Machine (pickup `SAVEPOINT`). In `g001` it's at -25.618 1.186 -1.515. Loading a save puts Scooby at -27 0.121 0 in `g001`.
- Mods: a folder in `UserData/mods` with `files` mirroring the disc and/or `textures`. When two change the same file, the later name wins.

**Rendering**
- The EFB isn't cleared at frame start (#78). The game's display copies never clear and its copy-clear colour is white, so suspect a clear for any "white frame" bug.
- Cut-outs: `[video] cutout_mode`, default two passes. Fringes are deferred and flushed before non-depth-writing and z-ALWAYS draws (#46).
- Frame interpolation (#92): pairing hashes POS array contents, not addresses, because the game rewrites CPU-skinned arrays into a buffer pool every frame. A pair moving more than 0.5 units in one frame with the camera still is a wrong pair. F3 recordings don't show interpolated frames.
- A launcher Display setting mirrors F10 Video (#91): a new F10 video setting with a lasting effect belongs in both.
- Monitor labels are duplicated in `aurora-main/lib/monitor_labels.hpp` and `launcher/Monitors.cs`: change both (#95).

**Audio**
- GC audio runs on the game thread. Volume sliders work through a native override of MIXAdjustFader (#86, `music_attenuation.cpp`). Known limits: resident dialog counts as SFX, Bink audio is master only.

**Paths**
- Portable (`data\portable.txt`): everything in `data\UserData`, disc in `data\Disc\DATA`. Installed: settings, logs, cache, disc and mods in `%LOCALAPPDATA%\FrightWorks-Data`, saves in `Saved Games\FrightWorks`. The installer's program folder is deleted on uninstall. `[paths] saves_dir` overrides the saves folder.

## Releases

Versions continue from upstream v1.0.2.

1. Version PR: `<Version>` in `launcher/ScoobyReMasked.Launcher.csproj` and the version line in `tools/package-README.txt`.
2. After the merge, on a clean `main`: local tag `v<ver>`, then `tools/package-scooby.ps1 -Build`, then `-Zip` as a second call (`-Build` resets PATH), then `-Installer`.
3. Smoke test: the zip has no game data, saves or settings; launcher `--shots` from the unzip; boot `h001` with HD and `i004`.
4. `gh release create v<ver> --draft --target <full SHA>` (a short SHA gets HTTP 422) with `FrightWorks-<ver>.zip`, every `dist\installer\*` file and the notes. A maintainer publishes.

Installed launchers only find updates in published releases carrying `releases.win.json`. A local test feed is described in `docs/testing.md`.
