# FrightWorks: Night of 100 Frights

A statically recompiled PC port of **Scooby-Doo! Night of 100 Frights** (GameCube, 2002). The game's PowerPC code is
translated ahead of time to C++ with [WiiCompiled](https://github.com/patchzyy/Wiicompiled) and compiled into a Windows
program, so there is no CPU emulator; the console's graphics, audio and system calls are reimplemented underneath.

FrightWorks continues [Scooby-Doo: Refrightened](https://github.com/josiahg2502-glitch/Scooby-Doo-Refrightened) by JosiahG. This repository is the GameCube version;
ports of the other console versions may follow.

![Mystic Manor in widescreen at 4K](docs/screenshots/courtyard.jpg)

<p>
  <img src="docs/screenshots/cinematic.jpg" width="32%" alt="Cinematic 2.39:1">
  <img src="docs/screenshots/menu.jpg" width="32%" alt="Main menu in widescreen">
  <img src="docs/screenshots/pause-ultrawide.jpg" width="32%" alt="Pause menu at 21:9 with Xbox button prompts">
</p>

## Features

- Up to 4K, true widescreen (16:9, fill, cinematic 2.39:1) or original 4:3
- 30 or 60 fps, MSAA anti-aliasing, HDR
- Restored Creeper window animation in Mystic Manor's Mystery Machine courtyard
- Built-in HD texture pack (can be switched off)
- Gamepads and keyboard, remappable; button prompts match your Xbox, PlayStation or Nintendo controller
- GameCube memory-card saves
- Option to skip the intro movies; mods that replace game files or textures
- The game's holiday events follow the real date, or pick a holiday or turn them off on the launcher's **Play** page

**No game data is included.** You need your own disc image.

The courtyard animation reuses the retained GameCube textures and their original timing, keeping the
GameCube window colors. No Xbox image or imported assets are needed; existing nonempty material-map mods
are left unchanged.

## Playing

Requires Windows 10/11 (64-bit) and **Night of 100 Frights, GameCube USA v1.01 (GIHE78, revision 1)** as `.iso`,
`.gcm` or `.ciso`.

1. Unzip the [latest release](https://github.com/Loneranger419/FrightWorks-no100f-gc/releases/latest).
2. Open `Scooby-Doo Refrightened.exe`, choose your disc image on the **Play** page, then press **PLAY**.

Settings are on the launcher's **Display** page and in game (**F10**). **F11** toggles fullscreen. Saves, settings
and mods live in `data\UserData`; unzip a new release over the old folder to update.

The GameCube audio runtime preserves a pending complete voice-address reset when subsequent loop, end,
current or looping updates arrive before synchronization. This avoids dropping the reset during movie
restarts; ordinary partial updates retain the original SDK behavior. This is a targeted synchronization
correction, not a guarantee that every intermittent audio issue is resolved.

Window-interaction stalls are separate: GameCube audio is generated on the game thread, and the
original Bink code can switch to silence when its audio-servicing timeout expires. The address
synchronization correction does not make audio continue through a blocked window event loop.

![The launcher](docs/launcher.png)

## Building

Windows 10/11 x64 with Python 3 and Git. The build downloads its own toolchain into the repo.

```powershell
powershell -ExecutionPolicy Bypass -File tools\build-launcher.ps1
& "launcher\publish\Scooby-Doo Refrightened Launcher.exe" --extract "C:\path\to\game.iso" Disc\DATA
powershell -ExecutionPolicy Bypass -File tools\build-no100f.ps1
powershell -ExecutionPolicy Bypass -File tools\package-scooby.ps1 -Zip
```

The game builds to `Play\`; the package goes to `dist\`. Packaging expects the HD texture pack in
`texture_pack\ScoobyHD` (not in git).

Audio polling retains elapsed host time, including
fractional microseconds, rather than dropping time after a 100 ms stall. Catch-up work remains
bounded per tick, and the original Bink safety thresholds are unchanged. This timing correction
does not guarantee that every historical audio failure is resolved. After changing the native AX
overrides, rebuild without `-SkipTranslate` so translated calls use the corrected setters.

## Credits

- FrightWorks: Loneranger419
- Original port, [Scooby-Doo: Refrightened](https://github.com/josiahg2502-glitch/Scooby-Doo-Refrightened): JosiahG
- [WiiCompiled](https://github.com/patchzyy/Wiicompiled) by patchzyy and contributors (GPLv3); Aurora by Luke Street
  (MIT); other components in [`wiicompiled/THIRD-PARTY-NOTICES.md`](wiicompiled/THIRD-PARTY-NOTICES.md)
- HD texture pack by **dogerki**
  ([Dolphin forums](https://forums.dolphin-emu.org/Thread-scooby-doo-night-of-100-frights-hd-texture-pack-ai))
- Button prompts from [Kenney's Input Prompts](https://kenney.nl/assets/input-prompts) (CC0)
- Launcher font "Scooby Doo" by Lauren Ashpole ([FontSpace](https://www.fontspace.com/scooby-doo-font-f7039))

## License

GPLv3 ([`LICENSE`](LICENSE)); bundled components keep their own licenses. FrightWorks is a modified version of
Scooby-Doo: Refrightened by JosiahG, changed by Loneranger419 from 2026-10-31 on (the git history lists every change).

Not covered by the GPL (these stay under their owners' terms):

- `launcher/art/`, `launcher/refrightened.ico`, `launcher/refrightened-256.png`, `launcher/scooby.ico`: Scooby-Doo and
  Shaggy artwork, © Hanna-Barbera / Warner Bros.
- `launcher/title.ttf`: the "Scooby Doo" font by Lauren Ashpole, freeware; commercial use requires a donation
  ([FontSpace](https://www.fontspace.com/scooby-doo-font-f7039))
- `docs/`: screenshots of the game and the launcher

Scooby-Doo ™ & © Hanna-Barbera / Warner Bros. Night of 100 Frights © THQ / Warner Bros. Interactive, developed by
Heavy Iron Studios. Unofficial fan project, not affiliated with or endorsed by them. The repository contains no game
code or assets; the game's data is read from your own copy.
