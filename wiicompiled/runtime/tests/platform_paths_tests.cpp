#include "platform/host_platform.h"

#include <filesystem>
#include <iostream>

int main() {
    const auto executableDirectory = RuntimePlatform::ExecutableDirectory();
    if (!executableDirectory) {
        std::cerr << "unable to resolve the current executable directory\n";
        return 1;
    }
    std::error_code directoryError;
    if (!executableDirectory->is_absolute() || !std::filesystem::is_directory(*executableDirectory, directoryError)) {
        std::cerr << "executable directory is not an existing absolute directory: " << *executableDirectory << '\n';
        return 1;
    }

    const auto userData = RuntimePlatform::ApplicationDataDirectory("WiiCompiledPlatformPathsTest");
    if (userData.filename() != "WiiCompiledPlatformPathsTest") {
        std::cerr << "application-data directory lost its application name: " << userData << '\n';
        return 1;
    }
    if (RuntimePlatform::LogDirectory("WiiCompiledPlatformPathsTest") != userData / "Logs") {
        std::cerr << "log directory is not derived from application data\n";
        return 1;
    }
#if defined(__APPLE__)
    if (userData.parent_path().filename() != "Application Support" ||
        userData.parent_path().parent_path().filename() != "Library") {
        std::cerr << "macOS application-data directory is not under ~/Library/Application Support: "
                  << userData << '\n';
        return 1;
    }
#endif

    return 0;
}
