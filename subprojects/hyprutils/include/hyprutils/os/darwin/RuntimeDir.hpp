#pragma once

#if defined(__APPLE__)

#include <expected>
#include <string>

namespace Hyprutils::OS::Darwin {
    std::string                             runtimeDirLinkPath();
    std::expected<std::string, std::string> runtimeDirTargetPath();
    std::expected<std::string, std::string> ensureRuntimeDirAt(const std::string& link, const std::string& target, bool create);
    std::expected<std::string, std::string> ensureRuntimeDir(bool create);
    std::expected<std::string, std::string> resolveRuntimeDir(bool create);
    std::expected<void, std::string>        exportRuntimeDir();
}

#endif
