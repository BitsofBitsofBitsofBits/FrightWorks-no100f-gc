#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

namespace no100f_devmenu {

inline constexpr uint32_t kWarpTable = 0x801C68B0;
inline constexpr uint32_t kGalleryScene = 0x48303033;
inline constexpr uint32_t kConditionCallback = 0x80045A3C;
inline constexpr std::array<uint32_t, 26> kWarpScenes = {
    0x42303034, 0x43303034, 0x45303034, 0x45303036, 0x45303039, 0x46303033, 0x46303037,
    0x46303130, 0x47303035, 0x47303038, 0x48303031, 0x49303033, 0x49303036, 0x4C303134,
    0x4C303138, 0x4F303034, 0x4F303036, 0x50303033, 0x50303035, 0x52303033, 0x53303032,
    0x57303232, 0x57303236, 0x4C303135, 0x47303031, 0x48303033,
};

inline bool GuestRange(uint32_t address, uint32_t size) {
    return address >= 0x80000000 && address < 0x81800000 && size <= 0x81800000 - address;
}

template <typename Read>
bool ReadSceneObjects(Read read, uint32_t list, uint32_t count, std::map<uint32_t, uint32_t>& objects) {
    objects.clear();
    if (count == 0 || count > 4000 || !GuestRange(list, count * 4)) return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t base, id;
        if (!read(list + i * 4, base) || !GuestRange(base, 16) || !read(base, id)) return false;
        const auto [it, inserted] = objects.emplace(id, base);
        // Stock H003 lists its player twice; identical references are not ambiguous IDs.
        if (!inserted && it->second != base) return false;
    }
    return true;
}

template <typename Read>
bool ReadWarps(Read read, std::array<uint32_t, 26>& flags) {
    for (size_t i = 0; i < kWarpScenes.size(); ++i) {
        const uint32_t address = kWarpTable + static_cast<uint32_t>(i) * 12;
        uint32_t index, scene;
        if (!read(address, index) || !read(address + 4, flags[i]) || !read(address + 8, scene) ||
            index != i || scene != kWarpScenes[i] || flags[i] > 1)
            return false;
    }
    return true;
}

template <typename Read, typename Write>
bool WriteWarp(Read read, Write write, size_t index, bool enabled, uint32_t& before) {
    std::array<uint32_t, 26> flags{};
    if (index >= kWarpScenes.size() || !ReadWarps(read, flags)) return false;
    before = flags[index];
    const uint32_t address = kWarpTable + static_cast<uint32_t>(index) * 12 + 4;
    uint32_t after;
    return write(address, enabled ? 1u : 0u) && read(address, after) && after == (enabled ? 1u : 0u);
}

enum class GallerySection { Pictures, Movies };
enum class GalleryRule { Natural, Locked, Unlocked };

inline const char* RuleName(GalleryRule rule) {
    switch (rule) {
    case GalleryRule::Natural: return "Game rules";
    case GalleryRule::Locked: return "Force locked";
    case GalleryRule::Unlocked: return "Force unlocked";
    }
    return "Unavailable";
}

struct ProgressCommand {
    enum class Kind { Invalid, Warps, Warp, Gallery, GallerySet };
    Kind kind = Kind::Invalid;
    size_t warp = 0;
    bool enabled = false;
    GallerySection section = GallerySection::Pictures;
    GalleryRule rule = GalleryRule::Natural;
};

inline ProgressCommand ParseProgressCommand(const std::string& command, const std::string& arguments) {
    ProgressCommand result;
    std::istringstream input(arguments);
    std::string first, second, extra;
    input >> first >> second;
    if (input >> extra) return result;
    if (command == "warps" && first.empty()) {
        result.kind = ProgressCommand::Kind::Warps;
    } else if (command == "gallery" && first.empty()) {
        result.kind = ProgressCommand::Kind::Gallery;
    } else if (command == "warp" && first.size() == 4 && (second == "on" || second == "off")) {
        uint32_t scene = 0;
        for (char c : first) {
            if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
            scene = (scene << 8) | static_cast<unsigned char>(c);
        }
        for (size_t i = 0; i < kWarpScenes.size(); ++i)
            if (scene == kWarpScenes[i]) {
                result.kind = ProgressCommand::Kind::Warp;
                result.warp = i;
                result.enabled = second == "on";
            }
    } else if (command == "gallery" && (first == "pictures" || first == "movies")) {
        if (second != "natural" && second != "locked" && second != "unlocked") return result;
        result.kind = ProgressCommand::Kind::GallerySet;
        result.section = first == "pictures" ? GallerySection::Pictures : GallerySection::Movies;
        result.rule = second == "natural" ? GalleryRule::Natural :
                      second == "locked" ? GalleryRule::Locked : GalleryRule::Unlocked;
    }
    return result;
}

struct ProgressAvailability {
    bool layout = false, inPlay = false, cinematic = false, movie = false, control = false, transfer = false;
    bool context = true;

    const char* Reason() const {
        if (!layout) return "Unsupported executable layout.";
        if (!inPlay) return "Requires stable gameplay.";
        if (!context) return "No safe game-thread callback context.";
        if (cinematic || movie) return "Cinematic/movie playing.";
        if (!control) return "Player does not have control.";
        if (transfer) return "Portal/travel/spot transfer pending.";
        return nullptr;
    }
};

inline constexpr uint32_t kPlayerHealth = 0x80245478;
inline constexpr uint32_t kPlayerSnacks = 0x80245744;
inline constexpr uint32_t kPlayerSda = 0x80278280;
inline constexpr uint32_t kSnackPayment = kPlayerSda - 0x76F0;
inline constexpr uint32_t kPlayerDying = kPlayerSda - 0x7724;
inline constexpr uint32_t kSnackAreaTable = 0x801D64F8;
inline constexpr uint32_t kSnackAreaCounts = 0x80245190 + 0xD7C;
inline constexpr uint32_t kDiagnosticSnackLimit = 99999;
inline constexpr std::array<std::array<uint32_t, 4>, 30> kSnackAreaScenes = {{
    {0, 0, 0, 0},
    {0x48303031, 0x48303032, 0x48303033, 0},
    {0x45303031, 0x45303032, 0x45303033, 0x45303034},
    {0x45303035, 0x45303036, 0, 0}, {0x45303037, 0x45303038, 0x45303039, 0},
    {0x43303031, 0x43303032, 0x43303033, 0x43303034}, {0x43303035, 0x43303036, 0x43303037, 0},
    {0x47303031, 0x47303032, 0, 0}, {0x47303033, 0x47303034, 0x47303035, 0x47303036},
    {0x47303037, 0x47303038, 0x47303039, 0}, {0x46303031, 0x46303033, 0, 0},
    {0x46303034, 0x46303035, 0x46303036, 0x46303037}, {0x46303038, 0x46303039, 0x46303130, 0},
    {0x4C303131, 0x4C303133, 0x4C303134, 0}, {0x4C303135, 0, 0, 0},
    {0x4C303137, 0x4C303138, 0x4C303139, 0}, {0x57303230, 0x57303231, 0x57303232, 0},
    {0x57303233, 0x57303235, 0x57303236, 0}, {0x57303237, 0x57303238, 0, 0},
    {0x49303031, 0x49303230, 0x49303231, 0x49303033}, {0x49303034, 0x49303035, 0x49303036, 0},
    {0x52303031, 0x52303230, 0x52303231, 0x52303033}, {0x52303034, 0x52303035, 0x52303036, 0},
    {0x4F303031, 0x4F303032, 0x4F303033, 0x4F303034}, {0x4F303035, 0x4F303036, 0x4F303038, 0},
    {0x42303031, 0x42303032, 0x42303033, 0x42303034}, {0x50303031, 0x50303032, 0x50303033, 0},
    {0x50303034, 0x50303035, 0, 0}, {0x53303031, 0x53303032, 0x53303033, 0},
    {0x53303034, 0x53303035, 0, 0},
}};

struct PlayerStats {
    uint32_t health = 0, snacks = 0, payment = 0, dying = 0;
    const char* Reason() const {
        if (health == 0 || dying != 0) return "Player is dead/dying; resurrection is not supported.";
        if (health > 5) return "Health is outside the verified 1..5 range.";
        if (payment != 0) return "Snack-gate payment pending.";
        return nullptr;
    }
};

template <typename Read>
bool ReadPlayerStats(Read read, PlayerStats& result) {
    return read(kPlayerHealth, result.health) && read(kPlayerSnacks, result.snacks) &&
           read(kSnackPayment, result.payment) && read(kPlayerDying, result.dying);
}

enum class PlayerStat { Health, Snacks };

template <typename Read, typename Write>
bool WritePlayerStat(Read read, Write write, PlayerStat stat, uint32_t value, uint32_t& before) {
    PlayerStats snapshot;
    if (!ReadPlayerStats(read, snapshot) || snapshot.Reason()) return false;
    if (stat == PlayerStat::Health ? value < 1 || value > 5 : value > kDiagnosticSnackLimit) return false;
    const uint32_t address = stat == PlayerStat::Health ? kPlayerHealth : kPlayerSnacks;
    before = stat == PlayerStat::Health ? snapshot.health : snapshot.snacks;
    uint32_t after;
    return write(address, value) && read(address, after) && after == value;
}

struct SnackArea {
    uint32_t title = 0, target = 0, collected = 0;
};
using SnackAreas = std::array<SnackArea, 30>;

template <typename Read>
bool ValidAreaTitle(Read read, uint32_t address, bool allowEmpty = false) {
    if (!GuestRange(address, 64)) return false;
    for (uint32_t offset = 0; offset < 64; offset += 4) {
        uint32_t word;
        if (!read(address + offset, word)) return false;
        for (int shift = 24; shift >= 0; shift -= 8) {
            const uint32_t c = word >> shift & 0xFF;
            if (c == 0) return allowEmpty || offset != 0 || shift != 24;
            if (c < 0x20 || c > 0x7E) return false;
        }
    }
    return false;
}

template <typename Read>
bool ReadSnackAreas(Read read, SnackAreas& areas) {
    for (uint32_t i = 0; i < areas.size(); ++i) {
        const uint32_t address = kSnackAreaTable + i * 36;
        uint32_t index;
        auto& area = areas[i];
        if (!read(address, area.title) || !ValidAreaTitle(read, area.title, i == 0) ||
            !read(address + 4, index) || index != i || !read(address + 8, area.target) ||
            area.target == 0 || area.target > kDiagnosticSnackLimit ||
            !read(kSnackAreaCounts + i * 0xD0, area.collected) || area.collected > kDiagnosticSnackLimit)
            return false;
        for (size_t j = 0; j < 4; ++j) {
            uint32_t scene;
            if (!read(address + 20 + static_cast<uint32_t>(j) * 4, scene) || scene != kSnackAreaScenes[i][j])
                return false;
        }
    }
    return true;
}

struct SnackConfirmation {
    bool active = false;
    uint32_t scene = 0, sceneId = 0, area = 0, before = 0, target = 0, title = 0, value = 0;
    void Clear() { active = false; }
    bool Stage(uint32_t scenePtr, uint32_t id, uint32_t index, uint32_t count, const SnackAreas& areas) {
        Clear();
        if (!GuestRange(scenePtr, 0x7C) || id == 0 || index == 0 || index >= areas.size() ||
            count > areas[index].target) return false;
        const auto& entry = areas[index];
        *this = {true, scenePtr, id, index, entry.collected, entry.target, entry.title, count};
        return true;
    }
    bool Matches(uint32_t scenePtr, uint32_t id, const SnackAreas& areas) const {
        return active && scene == scenePtr && sceneId == id && area > 0 && area < areas.size() &&
               before == areas[area].collected && target == areas[area].target && title == areas[area].title;
    }
};

template <typename Read, typename Write>
bool WriteSnackArea(Read read, Write write, const SnackConfirmation& pending, uint32_t scene, uint32_t sceneId) {
    PlayerStats player;
    SnackAreas areas{};
    if (!ReadPlayerStats(read, player) || player.Reason() || !ReadSnackAreas(read, areas) ||
        !pending.Matches(scene, sceneId, areas) || pending.value > areas[pending.area].target) return false;
    const uint32_t address = kSnackAreaCounts + pending.area * 0xD0;
    uint32_t after;
    return write(address, pending.value) && read(address, after) && after == pending.value;
}

inline bool ParseCount(std::string_view text, uint32_t maximum, uint32_t& result) {
    if (text.empty()) return false;
    uint32_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(c - '0');
        if (digit > maximum || value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
    }
    result = value;
    return true;
}

struct PlayerCommand {
    enum class Kind { Invalid, Read, Health, Snacks, Areas, Stage, Confirm, Cancel };
    Kind kind = Kind::Invalid;
    uint32_t area = 0, value = 0;
};
inline bool IsPlayerCommand(const std::string& command) {
    return command == "player" || command == "health" || command == "snacks" || command == "snackareas" ||
           command == "snackarea" || command == "snackconfirm" || command == "snackcancel";
}
inline PlayerCommand ParsePlayerCommand(const std::string& command, const std::string& arguments) {
    using Kind = PlayerCommand::Kind;
    PlayerCommand result;
    std::istringstream input(arguments);
    std::string first, second, extra;
    input >> first >> second;
    if (input >> extra) return result;
    if (first.empty()) {
        result.kind = command == "player" ? Kind::Read : command == "snackareas" ? Kind::Areas :
                      command == "snackconfirm" ? Kind::Confirm : command == "snackcancel" ? Kind::Cancel : Kind::Invalid;
    } else if (command == "health" && second.empty() && ParseCount(first, 5, result.value) && result.value != 0) {
        result.kind = Kind::Health;
    } else if (command == "snacks" && second.empty() && ParseCount(first, kDiagnosticSnackLimit, result.value)) {
        result.kind = Kind::Snacks;
    } else if (command == "snackarea" && ParseCount(first, 29, result.area) && result.area != 0 &&
               ParseCount(second, kDiagnosticSnackLimit, result.value)) {
        result.kind = Kind::Stage;
    }
    return result;
}

class GalleryRules {
public:
    void Observe(uint32_t scene, uint32_t id, uint32_t mode, bool valid) {
        // Pause (7) keeps the scene alive; load, title, new game and travel do not.
        if (!valid || id != kGalleryScene || (mode != 11 && mode != 7) || scene_ != scene) Clear();
        scene_ = valid && id == kGalleryScene ? scene : 0;
    }
    void Clear() { pictures = movies = GalleryRule::Natural; }
    GalleryRule Get(GallerySection section) const { return section == GallerySection::Pictures ? pictures : movies; }
    void Set(GallerySection section, GalleryRule rule) {
        (section == GallerySection::Pictures ? pictures : movies) = rule;
    }
private:
    uint32_t scene_ = 0;
    GalleryRule pictures = GalleryRule::Natural, movies = GalleryRule::Natural;
};

struct GalleryCondition {
    uint32_t id, variable, threshold, operation;
    uint32_t yesEvent, yesTarget, noEvent, noTarget;
    GallerySection section;
    bool visibility;
    uint32_t firstParameter = 0, secondParameter = 0;
};

struct PictureEntry { uint32_t condition, image; };
inline constexpr std::array<PictureEntry, 29> kPictureEntries = {{
    {0xB8ED42A5, 0x06C2C899}, {0xB8ED42A6, 0x06E51634}, {0xB8ED42A7, 0x070763CF},
    {0xB8ED42A8, 0x0729B16A}, {0xB8ED42A9, 0x074BFF05}, {0xB8ED42AA, 0x076E4CA0},
    {0xB8ED42AB, 0x07909A3B}, {0xB8ED42AC, 0x07B2E7D6}, {0xB8ED42AD, 0x07D53571},
    {0xB8ED4327, 0x77C3A58B}, {0xB8ED4328, 0x77E5F326}, {0xB8ED4329, 0x780840C1},
    {0xB8ED432A, 0x782A8E5C}, {0xB8ED432B, 0x784CDBF7}, {0xB8ED432C, 0x786F2992},
    {0xB8ED432D, 0x7891772D}, {0xB8ED432E, 0x78B3C4C8}, {0xB8ED432F, 0x78D61263},
    {0xB8ED4330, 0x78F85FFE}, {0xB8ED43AA, 0x89515BDC}, {0xB8ED43AB, 0x8973A977},
    {0xB8ED43AC, 0x8995F712}, {0xB8ED43AD, 0x89B844AD}, {0xB8ED43AE, 0x89DA9248},
    {0xB8ED43AF, 0x89FCDFE3}, {0xB8ED43B0, 0x8A1F2D7E}, {0xB8ED43B1, 0x8A417B19},
    {0xB8ED43B2, 0x8A63C8B4}, {0xB8ED43B3, 0x8A86164F},
}};

struct MovieEntry { uint32_t variable, display, image, playback, playbackArgument; };
// Logical movie identity follows the variable and outgoing UI link, not the condition's debug name.
inline constexpr std::array<MovieEntry, 14> kMovieEntries = {{
    {0xEC766001, 0x2268043D, 0x6BA97D87, 0x932FC9B5, 0x3F800000},
    {0xC04D0164, 0x1DEC4FB0, 0x6BCBCB22, 0xFA045F5E, 0x40000000},
    {0x9423A2C7, 0x19709B23, 0x6BEE18BD, 0x60D8F507, 0x40400000},
    {0x67FA442A, 0x14F4E696, 0x6C106658, 0xC7AD8AB0, 0x40800000},
    {0x3BD0E58D, 0x10793209, 0x6C32B3F3, 0x2E822059, 0x40A00000},
    {0x0FA786F0, 0x0BFD7D7C, 0x6C55018E, 0x9556B602, 0x40C00000},
    {0xE37E2853, 0x0781C8EF, 0x6C774F29, 0xFC2B4BAB, 0x40E00000},
    {0xB754C9B6, 0x03061462, 0x6C999CC4, 0x62FFE154, 0x41000000},
    {0x8B2B6B19, 0xFE8A5FD5, 0x6CBBEA5F, 0xC9D476FD, 0x41100000},
    {0x999E13F5, 0xDB9654A3, 0x7D14E63D, 0xCB23C987, 0x41200000},
    {0x1521F81E, 0xCE2336FC, 0x7D3733D8, 0x31F85F30, 0x41500000},
    {0x414B56BB, 0xD29EEB89, 0x7D598173, 0x98CCF4D9, 0x41400000},
    {0x6D74B558, 0xD71AA016, 0x7D7BCF0E, 0xFFA18A82, 0x41300000},
    {0xE8F89981, 0xC9A7826F, 0x7D9E1CA9, 0x6676202B, 0x41600000},
}};

inline constexpr size_t kGalleryConditionCount = kPictureEntries.size() + kMovieEntries.size() * 2;
inline GalleryCondition ConditionAt(size_t index) {
    if (index < kPictureEntries.size()) {
        const auto& picture = kPictureEntries[index];
        return {picture.condition, 0x09B72C09, static_cast<uint32_t>(index + 1), 3,
                0x5E, picture.image, 0x5E, 0x8F61ECA1, GallerySection::Pictures, false};
    }
    index -= kPictureEntries.size();
    const auto& movie = kMovieEntries[index / 2];
    const bool visibility = index % 2 == 0;
    return {visibility ? movie.display : movie.playback, movie.variable, 1, 0,
            visibility ? 4u : 0x126u, visibility ? movie.image : 0xB38273C3u,
            visibility ? 3u : 0x18u, visibility ? movie.image : 0xA7AD78EDu,
            GallerySection::Movies, visibility, visibility ? 0u : 0x40C00000u,
            visibility ? 0u : movie.playbackArgument};
}

template <typename Read, typename Find>
bool ValidateCondition(Read read, Find find, const GalleryCondition& expected, uint32_t& base) {
    base = find(expected.id);
    uint32_t id, kind, callback, asset, links;
    if (!GuestRange(base, 20) || !read(base, id) || id != expected.id ||
        !read(base + 4, kind) || kind >> 16 != 0x1F02 ||
        !read(base + 12, callback) || callback != kConditionCallback ||
        !read(base + 16, asset) || !GuestRange(asset, 84) ||
        !read(base + 8, links) || links != asset + 20)
        return false;
    const uint32_t words[] = {expected.id, kind, expected.threshold, expected.variable, expected.operation};
    for (uint32_t i = 0; i < 5; ++i) {
        uint32_t value;
        if (!read(asset + i * 4, value) || (i == 1 ? value >> 16 != 0x1F02 : value != words[i])) return false;
    }
    for (uint32_t link = 0; link < 2; ++link) {
        const uint32_t target = link ? expected.noTarget : expected.yesTarget;
        const uint32_t event = link ? expected.noEvent : expected.yesEvent;
        const uint32_t address = links + link * 32;
        uint32_t value, targetId;
        const uint32_t targetBase = find(target);
        if (!GuestRange(targetBase, 16) || !read(targetBase, targetId) || targetId != target ||
            !read(address, value) || value != ((link ? 0x3Eu : 0x3Du) << 16 | event) ||
            !read(address + 4, value) || value != target)
            return false;
        for (uint32_t offset = 8; offset < 32; offset += 4) {
            const uint32_t parameter = link == 0 && offset == 8 ? expected.firstParameter :
                                       link == 0 && offset == 12 ? expected.secondParameter : 0;
            if (!read(address + offset, value) || value != parameter) return false;
        }
    }
    return true;
}

template <typename Evaluate, typename Reset, typename Dispatch>
uint32_t ConditionalEvent(uint32_t target, uint32_t event, GalleryRule rule,
                          Evaluate evaluate, Reset reset, Dispatch dispatch) {
    if (event == 0x3C) {
        const bool passed = rule == GalleryRule::Natural ? evaluate(target) : rule == GalleryRule::Unlocked;
        dispatch(target, passed ? 0x3Du : 0x3Eu);
    } else if (event == 0x0A) {
        reset(target);
    }
    return 1;
}

} // namespace no100f_devmenu
