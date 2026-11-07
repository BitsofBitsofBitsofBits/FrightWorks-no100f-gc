# GPU/state bug case histories

## Issues #6 (manor curtains) + #2 (solid map textures) — PR #8 `fix/issue-6`, 1st commit `7a3d14e`

- Game builds mesh display lists at load in `0x8015E944` (callers `0x8015DE5C` / `0x8015E47C`, RenderWare-style instancing): per
  mesh it budgets `round32(3 + n*attrs*idxSize)` (+32 if an 8-bit list is an exact multiple of 32), lays the vertex arrays right
  after the DL arena, packs lists with `ptr += GXEndDisplayList()`.
- Real `GXBeginDisplayList` (`0x801022C8`) calls `__GXSetDirtyState` (`0x800FE204`) when `__GXData(r13-0x7DB8)->dirtyState(+0x4F4)`
  is set, **before** redirecting the FIFO. Override `No100f_GXBeginDisplayList` (`hle/gx/gx_fifo.cpp`) skipped it, so the game's
  `GXBegin` recorded 21 bytes of vertex state (`CP 50=7E00, CP 60=0, XF 1008=5`) **into** each list (curtain strip 328 B → 307 B after fix).
  - #6: lists overran their budget into per-frame (CPU-skinned) vertex arrays; floats overwrote the last indices → thin neck + long tail.
  - #2: load-time GX state replayed on every list call → floors/walls lose texgens (mechanism inferred; `e001` trigger clean after fix).
- Evidence: `NO100F_DLTAIL` showed 60+ lists diverging exactly at the budget offset with float bytes; after the fix 0.
- Verified: human `i001` + `e001`; smoke `h001`. Also fixes issue #4's invisible spider web (bashable web wall, helmet power-up
  area), per the human; same mechanism presumably, not separately logged.
- Do not repeat: the pre-fix curtain chase (panels in pools `0xB94FA0`/`0xBA19C0`, strip/tri counts, skips, PNMTX identity,
  force-LE, clamps) attributed the wrong draws (static props). See `manual-yn-bisect/examples.md`.

## PR #8 regression: i004 ghosts invisible — PR #8 2nd commit `622b97a`

- Symptom (PR #8 alone): first ghost monsters in `i004` vanish at certain spots ("crossing over planes") but still hurt Scooby.
  `main` and Dolphin: always visible.
- **Root cause:** aurora's API VCD shadow (`__gx->vcdLo/Hi`) went stale. The game's CP VCD writes reach aurora's decoder via
  `GxCpDecodeForwardToAurora` without touching the shadow, so a TEX0 = DIRECT published earlier by an API path stayed there.
  `ApplyAuroraVtxDesc` (`gx_dl.cpp`) only checked the shadow (`AuroraVtxDescMatchesLive`) for indexed attrs, so it never cleared
  it. The next dirty flush (`__GXSetVCD`) re-sent the whole shadow, and the ghost lists (VCD 7E00, 6-byte vertex) were decoded as
  14-byte vertices. Pre-#8 the lists began with their own CP 50/60 writes, which reset the decoder and hid it.
- **Fix:** check every attr ≤ TEX7 against the shadow. Same commit removed aurora's DL TailSkip heuristic (it compensated for the
  #6 overrun). Probes: `[livevtx]` (decoded layout vs HLE after each CP-free list call) ~1.4k/200k bad → 0; `[shadowvcd]` (shadow
  vs decoder at each `__GXSetVCD`) `hi 0001 vs 0000` → none. Human: ghosts visible in i004.
- **Method that worked:** identify the draws first (per-list flat paint), then compare what aurora *decoded* with what the HLE
  meant to publish, per list call. Probes: `tools/debug/patches/gx-draw-probes.patch` (see `SKILL.md`).
- **Trap that cost a day:** the draws first probed (`80B6AA00/B5C0/BC00`) were the curtains, not the ghost. Ghost model lists =
  `80B2B6C0`/`+0x5BB`/`+0x3981`; Scooby = `80B9DB80`/`80BB4220`. Paint a draw before trusting that it is the one on screen.
- Ruled out first (each A/B'd in i004): CP dedup cache, forced flatten, draw merging, vertex-size cache resets, stale pipeline memo,
  stale write-generation caches, list contents changing after build, `GXEndDisplayList` 32-byte padding alone. Whole-frame state
  dumps were identical between vanish and visible runs because the bad state was in aurora's *decoder*, not in `g_gxState`.
- Latent sibling (not seen yet): `ApplyAuroraVtxAttrFmt` trusts its mirror the same way; a stale VAT in the shadow would re-emit via
  `__GXSetVAT`. `[livevtx]` also checks VAT for the list's format and found 0.
- Side facts: native `GXEndDisplayList` returns the unpadded byte count (SDK pads to 32 with NOPs); the builder reserves exactly
  sum(round32(size)), so padding + any extra prefix overflows the last list. Native Begin/End skip the SDK's `dlSaveContext`
  (`+0x4F1`) save/restore. Scooby and a few models get a second camera-fixed pass per frame (off-screen render copied to a texture).
  Models are rebuilt during play (dirty Begins #39+) at heap slots that differ run to run.

Parked follow-ups (only if a new bug ties in; each its own A/B):
- Native `GXEndDisplayList` (`0x80102390`) skips the end dirty flush and returns exact (not 32-byte padded) size.
- `GXTexObj_` Image0 1024 decode (dropped from closed PR #5; upstream test asserts the wrap) — only with a real 1024 texture bug.

## Issue #9 (dotted outlines on light shafts) — PR #10, `fix/issue-9` `5888644`

- Symptom: bright dotted lines along additive light shafts/pools (foyer beams `i001`, floor lamp in fireplace room `i020`).
- Evidence: 0 GX line draws (`NO100F_SKIP_LINES=2`); magenta EFB clear (`clear-magenta-diagnostic.patch`) left dots shaft-coloured
  (not holes); `msaa = 1` → gone; `msaa = 4` + centroid → gone.
- Fix: `@interpolate(perspective, centroid)` on every varying in `aurora-main/lib/gx/shader.cpp` (`vtxOutAttrs`), as Dolphin does
  under MSAA. Verified with 120 fps interpolation and combined with PR #8. Levels outside the manor not checked.

## Real date + Holidays option — PR #12 `fix/real-date` `e462179`, PR #13 `feat/holiday-modes` `e1690f6` (independent)

- Symptom: hubs fired fireworks every load (and their SFX pushed the h002 mix into the limiter). Cause: `GetTimeBase()`
  (`ppc_helpers.cpp`) started at 0 → `OSTicksToCalendarTime` = 2000-01-01. Real GC: IPL sets the TB from the RTC (local time,
  ticks since 2000-01-01, 40.5 MHz). #12 adds that epoch (DetClock untouched). `OSGetTime` override reads the TB directly;
  alarms use `ReadSystemTime` (TB + `0x800030D8` base), all shift together.
- Holiday code `fn_800AED5C` (runs on level load, only if level code == `H001`/`H002`, strings at `r2-0x65d8`/`-0x6570`):
  month jump table `0x801D4790` (month from `fn_80016644` = month+1, day from `fn_80016674`; **nothing else calls either**).
  Fireworks (`PAREMIT_FIREWORKS_TRAIL/1/2`) Jan 1, Feb 2, Mar 22, May 30, Aug 28, Sep 8, Nov 5, Dec 20, Dec 31; Mar 17
  `fn_800AEB88` green chimney smoke (objects named "FOUNTAIN EMITTER*"); Jul 4 `fn_800AE9CC` RWB smoke + fireworks; Oct 31
  unhides "BAT HANG 0..19" (bats over the manor porch, h001 — subtle); Dec 14 / Dec 25 snow (weather object `r13-0x731c`,
  `fn_80020E94`); other December days set the snow object up with no visible snow. Months 4 and 6 have no handler.
- #13: native overrides of the two getters (`hle/no100f_holidays.cpp`) answer `[game] holiday`: "auto" = host local date,
  "off" = 06-15, "MM-DD" (validated in `runtime_config.h` `IsSupportedHoliday`). Launcher Play page dropdown (`MainForm.cs`
  `Holidays`): today / off / New Year's / St. Patrick's / Fourth of July / Halloween / Christmas; unknown value → "Custom date".
- Verified by screenshots (`tools/debug/holiday-shots.ps1`): h001/h002 each date; #13 alone with "auto" → no fireworks.

Game-logic / frame-rate cases (PR #14 fog flicker) are in the `game-logic-debug` skill.
