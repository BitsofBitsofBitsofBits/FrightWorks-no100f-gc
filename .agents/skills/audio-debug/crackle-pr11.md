# Music crackle — PR #11 (2026-10-23)

Reported by Easy301 in issue #2 comments (Dolphin + Deck + recomp, not real hardware; worst in Mystic Playground `h002`).

- Idle `h002` capture, 95 s: 28 clicks, L = R, no gaps/flat tops, on a 0.875 s grid (= every ring wrap, multiples of 175 frames).
- Cause: at a stream wrap `AcceleratorT::ReadSample` reloaded pred/scale from `adpcm_loop_info` (always 0 in this game) while the
  ARAM header at the loop block said e.g. `0x69`/`0x77`/`0x15` (54/54 wraps differed) → first block after each wrap decoded with
  the wrong coefs/scale.
- Fix `d5146ed` (`ax_internal.h` +6/-1): ADPCM stream voices take pred/scale from `ReadAram8((loop & ~15) >> 1)` on wrap;
  yn1/yn2 stay continuous. Non-stream loops and PCM streams unchanged.
- Result: stream clicks 28/62 s on-grid → 7/95 s off-grid (musical); final output 28 → 4 weak. Human: "way better".
- Remaining fuzz in the first ~13 s: in the disc asset. Runtime decode is bit-exact to `adpcm_ref.py` (576k samples); sinc SRC
  (offline A/B, script not kept) changes nothing measurable. Human hasn't A/B'd the disc decode by ear.
- Side findings: no depop values ever; SFX voices end cleanly; 0 backend drops in `h002`; pre-limiter bus up to 41,479 with
  fireworks (19.5k with real date).
- PR body has before/after `.mp4` clips (17–27 s, music voice only). Easy301 pinged on #2.

Follow-ups: the fireworks became PR #12 (real date) + PR #13 (Holidays option).

Limiter overload — looked at and dropped (2026-10-24):
- `GcOutputSamples` tanh waveshaper (knee 24576) was added by the maintainer in 1.0.2 for Bink movies (+4 dB over near-full-scale audio).
- `main` build, `h002` with constant fireworks, 98 s: pre-limiter peak 48,582 (+3.4 dB), only 0.37% of samples above the knee, in
  short bursts on bangs.
- Offline prototype (script not kept): 2 ms look-ahead
  gain limiter (window-min hold + release + box attack, ceiling 32000) vs current tanh → `look.wav` / `tanh.wav`. Human A/B on a
  30 s clip: no audible difference. With #12/#13 fireworks are rare anyway. Reopen only if someone reports harsh audio in a
  specific scene (capture that scene's `bus.raw` first; movies were never measured).
