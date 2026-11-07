#pragma once
// Stream voices (music): DVD read callback fn_800DA71C, ported from the game with one fix.
//
// The refill callback fn_800D9EF8 detects the end of a track by comparing bytes read (entry +0x90) with +0x94 each time
// the voice crosses into the first half of its ARAM ring: equal means no read since the last crossing, so the track
// ended; otherwise it stores bytes read in +0x94. The stream start fn_800D8CA8 resets +0x90 but never +0x94. The first
// crossing is always at read 0xBB80, so a stream stopped after exactly one crossing leaves 0xBB80 behind, and the next
// stream in that entry looks finished at its first crossing. The restart hits the same check and the equal branch never
// stores, so the music repeats about a second of its start until the game is closed (issue 14).
//
// Bytes read are 0 only before a stream's first chunk, on every path through fn_800D8CA8, and all three read sites use
// this callback, so this is where +0x94 gets cleared. A real track end still compares equal at a later crossing.
#include <cstdint>

namespace No100fStreamRead {

constexpr uint32_t kFlags = 0x04, kPosition = 0x88, kBytesRead = 0x90, kPrevRead = 0x94;
constexpr uint32_t kMainBuffer = 0xD4, kAramHalf1 = 0xD8, kAramHalf2 = 0xDC, kRequest = 0xE0;
constexpr uint32_t kFlagStarting = 0x4, kFlagReadBusy = 0x100, kFlagCopy1Busy = 0x200, kFlagCopy2Busy = 0x400,
                   kFlagChunkReady = 0x800, kFlagHalf1Full = 0x1000, kFlagHalf2Full = 0x2000;
constexpr uint32_t kChunkBytes = 0x2EE0;

struct Result {
    bool wakeRefill = false;     // a copy to ARAM is in flight: set the game's refill flag (r13 - 0x7258)
    uint32_t copyDest = 0;       // nonzero: post an ARQ copy of the chunk to this ARAM address
};

// read32/write32 take guest addresses. entry is the stream table entry; bytes is the callback's result.
template <class Read32, class Write32>
Result OnChunkRead(uint32_t entry, uint32_t bytes, Read32&& read32, Write32&& write32) {
    Result result;
    if (read32(entry) == 0) return result;  // voice already freed
    write32(entry + kPosition, read32(entry + kPosition) + bytes);
    const uint32_t before = read32(entry + kBytesRead);
    if (before == 0) write32(entry + kPrevRead, 0);  // the fix: this stream's first chunk
    write32(entry + kBytesRead, before + bytes);

    uint32_t flags = (read32(entry + kFlags) | kFlagChunkReady) & ~kFlagReadBusy;
    result.wakeRefill = (flags & (kFlagCopy1Busy | kFlagCopy2Busy)) != 0;
    if (flags & kFlagStarting) {
        if (!(flags & kFlagHalf1Full)) {
            flags |= kFlagCopy1Busy;
            result.copyDest = read32(entry + kAramHalf1);
        } else if (!(flags & kFlagHalf2Full)) {
            flags |= kFlagCopy2Busy;
            result.copyDest = read32(entry + kAramHalf2);
        }
    }
    write32(entry + kFlags, flags);
    return result;
}

}  // namespace No100fStreamRead
