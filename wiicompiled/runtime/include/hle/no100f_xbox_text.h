#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Original Xbox controls (hle/no100f_xbox_layout.h): the tutorial hints that name GameCube buttons in words get the
// Xbox layout's buttons. Hints that show a button picture ($D#$C and so on) need nothing: those pictures come from the
// prompt atlas, which follows the layout. The level files (HIP) are patched as the game reads them (hle/storage/dvd.cpp).
namespace No100fXboxText {

// The buttons the hints name: Black (sneak) and White (soap bubble), as Xbox button names ("RB", "Y", "D-pad Up").
// Empty = that action has no button: its hints keep the GameCube text.
struct Names {
    std::string black = "RB", white = "LB";
};

// The Xbox name of a pad button (a controller_button_names.h config name), for the hints; "" when it has none.
inline std::string ButtonName(std::string_view configName) {
    configName = configName.substr(0, configName.find('@'));
    static constexpr std::pair<std::string_view, std::string_view> kNames[] = {
        {"south", "A"}, {"east", "B"}, {"west", "X"}, {"north", "Y"},
        {"left_shoulder", "LB"}, {"right_shoulder", "RB"}, {"left_trigger", "LT"}, {"right_trigger", "RT"},
        {"back", "Back"}, {"start", "Start"}, {"guide", "Guide"},
        {"left_stick", "Left Stick"}, {"right_stick", "Right Stick"},
        {"dpad_up", "D-pad Up"}, {"dpad_down", "D-pad Down"}, {"dpad_left", "D-pad Left"}, {"dpad_right", "D-pad Right"},
        {"left_paddle1", "P3"}, {"left_paddle2", "P4"}, {"right_paddle1", "P1"}, {"right_paddle2", "P2"},
        {"misc1", "Share"},
    };
    for (const auto& [config, name] : kNames) {
        if (config == configName) return std::string(name);
    }
    return {};
}

struct Replacement {
    std::string from, to;
};

// In this order: "Z + R" before plain "R". Sneak = Black, soap bubble = White, run = right trigger.
inline std::vector<Replacement> Replacements(const Names& names) {
    std::vector<Replacement> out;
    if (!names.black.empty()) {
        out.push_back({"the Z + R Button", "the " + names.black + " Button"});                     // i003 slippers
        out.push_back({"the Z Button and R Button at the same time", "the " + names.black + " Button"});  // r004, e002
    }
    if (!names.white.empty()) {
        out.push_back({"the Z Button and L Button at the same time", "the " + names.white + " Button"});  // b004 bubble
    }
    out.push_back({"the R Button", "the RT Button"});                                              // h002 run
    return out;
}

struct Patch {
    uint32_t offset = 0;  // in the file
    std::vector<uint8_t> bytes;
};

inline uint32_t ReadBe32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

inline std::string Rewrite(std::string text, const Names& names = {}) {
    for (const auto& [from, to] : Replacements(names)) {
        for (size_t at = text.find(from); at != std::string::npos; at = text.find(from, at + to.size())) {
            text.replace(at, from.size(), to);
        }
    }
    return text;
}

// TEXT asset = big-endian u32 length (with the terminating zero), then the text. The new text has to fit the asset.
inline void PatchTextAsset(const uint8_t* data, size_t size, uint32_t offset, uint32_t assetSize, const Names& names,
                           std::vector<Patch>& patches) {
    if (assetSize < 5 || offset > size || assetSize > size - offset) return;
    const uint8_t* asset = data + offset;
    const uint32_t length = ReadBe32(asset);
    if (length == 0 || length > assetSize - 4) return;
    const char* chars = reinterpret_cast<const char*>(asset + 4);
    const std::string text(chars, std::find(chars, chars + length, char(0)));
    const std::string rewritten = Rewrite(text, names);
    if (rewritten == text || rewritten.size() + 1 > assetSize - 4) return;
    Patch patch{offset, std::vector<uint8_t>(assetSize, 0)};
    const uint32_t newLength = static_cast<uint32_t>(rewritten.size() + 1);
    patch.bytes[0] = uint8_t(newLength >> 24), patch.bytes[1] = uint8_t(newLength >> 16);
    patch.bytes[2] = uint8_t(newLength >> 8), patch.bytes[3] = uint8_t(newLength);
    std::memcpy(patch.bytes.data() + 4, rewritten.data(), rewritten.size());
    patches.push_back(std::move(patch));
}

// HIP layout: HIPA, then chunks (4-byte tag, big-endian size); DICT > ATOC > AHDR (id, type, file offset, size).
inline void Walk(const uint8_t* data, size_t size, size_t offset, size_t end, const Names& names, std::vector<Patch>& patches,
                 int depth = 0) {
    while (depth < 8 && offset + 8 <= end) {
        const uint8_t* tag = data + offset;
        const uint32_t chunkSize = ReadBe32(data + offset + 4);
        const size_t body = offset + 8;
        if (std::memcmp(tag, "HIPA", 4) == 0) {
            offset = body;
            continue;
        }
        if (chunkSize > end - body) return;
        if (std::memcmp(tag, "DICT", 4) == 0 || std::memcmp(tag, "ATOC", 4) == 0) {
            Walk(data, size, body, body + chunkSize, names, patches, depth + 1);
        } else if (std::memcmp(tag, "AHDR", 4) == 0 && chunkSize >= 16 && std::memcmp(data + body + 4, "TEXT", 4) == 0) {
            PatchTextAsset(data, size, ReadBe32(data + body + 8), ReadBe32(data + body + 12), names, patches);
        }
        offset = body + chunkSize;
    }
}

inline std::vector<Patch> FindPatches(const uint8_t* data, size_t size, const Names& names = {}) {
    std::vector<Patch> patches;
    if (size >= 4 && std::memcmp(data, "HIPA", 4) == 0) Walk(data, size, 0, size, names, patches);
    return patches;
}

// `buffer` holds the file's bytes from `fileOffset` on.
inline void Apply(const std::vector<Patch>& patches, uint64_t fileOffset, std::vector<uint8_t>& buffer) {
    const uint64_t end = fileOffset + buffer.size();
    for (const auto& patch : patches) {
        const uint64_t from = std::max<uint64_t>(patch.offset, fileOffset);
        const uint64_t to = std::min<uint64_t>(patch.offset + patch.bytes.size(), end);
        if (from < to) {
            std::memcpy(buffer.data() + (from - fileOffset), patch.bytes.data() + (from - patch.offset), to - from);
        }
    }
}

} // namespace No100fXboxText
