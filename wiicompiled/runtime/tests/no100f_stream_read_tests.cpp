#include "hle/no100f_stream_read.h"

#include <iostream>
#include <stdexcept>
#include <unordered_map>

using namespace No100fStreamRead;

static void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

namespace {
constexpr uint32_t kEntry = 0x8024C258u;

struct Guest {
    std::unordered_map<uint32_t, uint32_t> mem;
    Guest() {
        mem[kEntry] = 0x8026C098u;
        mem[kEntry + kAramHalf1] = 0xEA60;
        mem[kEntry + kAramHalf2] = 0x11940;
    }
    uint32_t& At(uint32_t field) { return mem[kEntry + field]; }
    Result Read(uint32_t bytes = kChunkBytes) {
        return OnChunkRead(kEntry, bytes, [this](uint32_t a) { return mem[a]; },
                           [this](uint32_t a, uint32_t v) { mem[a] = v; });
    }
    // The end check at a crossing into half 1 (fn_800D9EF8, 800DA058): returns true when the track looks finished.
    bool CrossHalf1() {
        if (At(kBytesRead) == At(kPrevRead)) return true;
        At(kPrevRead) = At(kBytesRead);
        return false;
    }
};
}  // namespace

int main() {
    try {
        // Issue 14: the previous stream stopped after one crossing, leaving 0xBB80. The next stream must not look finished.
        {
            Guest g;
            g.At(kPrevRead) = 0xBB80;
            for (int i = 0; i < 4; ++i) g.Read();
            Require(g.At(kBytesRead) == 0xBB80, "four chunks read");
            Require(!g.CrossHalf1(), "a new stream's first crossing is not the end of the track");
            Require(g.At(kPrevRead) == 0xBB80, "the crossing stores bytes read");
        }
        // Only the first chunk clears it, and a real end still compares equal.
        {
            Guest g;
            g.Read();
            g.At(kPrevRead) = 0x1234;
            g.Read();
            Require(g.At(kPrevRead) == 0x1234, "later chunks leave the previous read alone");
            g.At(kPrevRead) = 0;
            Require(!g.CrossHalf1(), "still reading");
            Require(g.CrossHalf1(), "no read since the last crossing is the end of the track");
        }
        // The rest of the callback, as in fn_800DA71C.
        {
            Guest g;
            g.At(kPosition) = 0x40966E0;
            g.At(kFlags) = kFlagReadBusy | kFlagStarting;
            const Result first = g.Read();
            Require(g.At(kPosition) == 0x40966E0 + kChunkBytes, "position advances");
            Require(g.At(kFlags) == (kFlagChunkReady | kFlagStarting | kFlagCopy1Busy), "chunk ready, copy to half 1");
            Require(first.copyDest == 0xEA60, "first chunk goes to half 1");
            Require(!first.wakeRefill, "no copy was in flight when the chunk arrived");

            g.At(kFlags) = kFlagStarting | kFlagHalf1Full;
            const Result second = g.Read();
            Require(second.copyDest == 0x11940, "second chunk goes to half 2");
            Require(g.At(kFlags) == (kFlagChunkReady | kFlagStarting | kFlagHalf1Full | kFlagCopy2Busy), "copy to half 2");

            g.At(kFlags) = kFlagStarting | kFlagHalf1Full | kFlagHalf2Full;
            Require(g.Read().copyDest == 0, "both halves full: no copy");

            g.At(kFlags) = kFlagReadBusy;
            const Result running = g.Read();
            Require(running.copyDest == 0 && !running.wakeRefill, "running stream: the refill callback does the copy");
            Require(g.At(kFlags) == kFlagChunkReady, "read busy cleared");

            g.At(kFlags) = kFlagCopy1Busy;
            Require(g.Read().wakeRefill, "a copy in flight wakes the refill");
        }
        // A freed voice changes nothing.
        {
            Guest g;
            g.At(0) = 0;
            g.At(kFlags) = kFlagReadBusy;
            g.At(kBytesRead) = 0x2EE0;
            const Result result = g.Read();
            Require(result.copyDest == 0 && !result.wakeRefill, "no copy");
            Require(g.At(kBytesRead) == 0x2EE0 && g.At(kFlags) == kFlagReadBusy, "no state change");
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "no100f_stream_read_tests passed\n";
    return 0;
}
