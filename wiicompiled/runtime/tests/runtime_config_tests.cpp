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
    // Optional persistence checks require a caller-owned portable copy of this test executable.
    // Validate the exact expected path before any function that can create installed user data.
    if (argc == 2) {
        const auto executable = RuntimeConfigFile::ExecutableDirectory();
        const auto expected = std::filesystem::absolute(argv[1]).lexically_normal();
        if (!Require(executable && std::filesystem::is_regular_file(*executable / "portable.txt") &&
                     expected == (*executable / "UserData" / "Config.toml").lexically_normal(),
                     "persistence tests require an isolated executable, portable marker and exact fixture config path")) return 1;
        if (!Require(RuntimeConfigFile::ResolveConfigPath() == expected, "portable config did not resolve to the owned fixture")) return 1;
        std::filesystem::create_directories(expected.parent_path());
        {
            std::ofstream initial(expected);
            initial << "[launcher]\nauto_controller_layout = true\n"
                       "[controller]\nlayout = \"xbox_original\"\na = \"north@30,east\"\n"
                       "xbox_black = \"left_paddle1\"\n[future]\nvalue = \"keep\"\n";
            if (!Require(static_cast<bool>(initial), "could not initialize fixture config")) return 1;
        }
        for (bool original : {true, false}) {
            if (!Require(RuntimeConfigFile::WriteSetting("launcher", "auto_controller_layout", "true"),
                         "could not reset fixture auto intent")) return 1;
            if (!Require(RuntimeConfigFile::SetXboxOriginalLayout(original), "manual F10 scheme write failed")) return 1;
            const auto document = toml::parse(expected.string());
            if (!Require(!toml::find<bool>(document, "launcher", "auto_controller_layout"),
                         "manual scheme selection did not stop automatic matching")) return 1;
            if (!Require(toml::find<std::string>(document, "controller", "layout") ==
                         (original ? "xbox_original" : "gamecube"), "manual scheme was not persisted")) return 1;
            if (!Require(toml::find<std::string>(document, "controller", "a") == "north@30,east" &&
                         toml::find<std::string>(document, "controller", "xbox_black") == "left_paddle1" &&
                         toml::find<std::string>(document, "future", "value") == "keep",
                         "manual scheme selection changed custom bindings or unrelated data")) return 1;
        }
        std::cout << "PASS F10 same-value/changed-layout manual intent and binding preservation\n";
    } else if (argc != 1) {
        std::cerr << "usage: runtime_config_tests [absolute portable fixture Config.toml]\n";
        return 1;
    }
    return 0;
}
