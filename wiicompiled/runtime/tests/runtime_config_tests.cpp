#include "runtime_config.h"

#include <iostream>
#include <sstream>

namespace {
bool Require(bool value, const char* message) {
    if (!value) {
        std::cerr << message << '\n';
        return false;
    }
    return true;
}
} // namespace

int main(int argc, char** argv) {
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = true\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "enabled.toml");
        if (!Require(config.metalFxSpatialUpscaling == std::optional<bool>{true},
                     "valid MetalFX setting was not loaded")) return 1;
    }
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = false\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "disabled.toml");
        if (!Require(config.metalFxSpatialUpscaling == std::optional<bool>{false},
                     "false MetalFX setting was not loaded")) return 1;
    }
    {
        std::istringstream input("[video]\nmetalfx_spatial_upscaling = \"yes\"\n");
        const RuntimeUserConfig config = RuntimeConfigFile::ParseConfig(input, "invalid.toml");
        if (!Require(!config.metalFxSpatialUpscaling, "invalid MetalFX setting was accepted")) return 1;
    }
    // Persistence is opt-in and must use a caller-owned portable copy of this CPU test.
    if (argc == 2) {
        const auto executable = RuntimeConfigFile::ExecutableDirectory();
        const auto expected = std::filesystem::absolute(argv[1]).lexically_normal();
        if (!Require(executable && std::filesystem::is_regular_file(*executable / "portable.txt") &&
                     expected == (*executable / "UserData" / "Config.toml").lexically_normal(),
                     "persistence checks require a portable marker and exact owned fixture path")) return 1;
        if (!Require(RuntimeConfigFile::ResolveConfigPath() == expected, "portable config path mismatch")) return 1;
        std::filesystem::create_directories(expected.parent_path());
        {
            std::ofstream initial(expected);
            initial << "[launcher]\nauto_controller_layout = true\nauto_controller_layout_value = \"xbox_original\"\n"
                       "[video]\nhdr = false\nhdr_paper_white_nits = 237.5\nhdr_peak_nits = 1234.5\n"
                       "[controller]\nlayout = \"xbox_original\"\na = \"north@30,east\"\n"
                       "xbox_black = \"left_paddle1\"\n[future]\nvalue = \"keep\"\n";
            if (!Require(static_cast<bool>(initial), "could not initialize fixture config")) return 1;
        }
        for (bool original : {true, false}) {
            if (!Require(RuntimeConfigFile::WriteSetting("launcher", "auto_controller_layout", "true"),
                         "could not reset automatic intent")) return 1;
            if (!Require(RuntimeConfigFile::SetXboxOriginalLayout(original), "manual F10 preset write failed")) return 1;
            const auto document = toml::parse(expected.string());
            if (!Require(!toml::find<bool>(document, "launcher", "auto_controller_layout") &&
                         toml::find<std::string>(document, "controller", "layout") ==
                         (original ? "xbox_original" : "gamecube"),
                         "same-value or changed manual preset did not stop automatic matching")) return 1;
            if (!Require(toml::find<std::string>(document, "launcher", "auto_controller_layout_value") == "xbox_original" &&
                         toml::find<std::string>(document, "controller", "a") == "north@30,east" &&
                         toml::find<std::string>(document, "controller", "xbox_black") == "left_paddle1" &&
                         toml::find<std::string>(document, "future", "value") == "keep" &&
                         !toml::find<bool>(document, "video", "hdr") &&
                         toml::find<double>(document, "video", "hdr_paper_white_nits") == 237.5 &&
                         toml::find<double>(document, "video", "hdr_peak_nits") == 1234.5,
                         "manual preset changed remembered intent, HDR, bindings or unrelated data")) return 1;
        }
        std::cout << "PASS same-value/changed F10 manual intent and cross-setting preservation\n";
    } else if (argc != 1) {
        std::cerr << "usage: runtime_config_tests [absolute portable fixture Config.toml]\n";
        return 1;
    }
    return 0;
}
