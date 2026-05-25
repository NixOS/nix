#pragma once

#include <filesystem>

namespace nix::darwin::detail {

std::filesystem::path profileAppBundlesDirAt(const std::filesystem::path & profile, const std::filesystem::path & base);

void syncProfileAppBundlesAt(
    const std::filesystem::path & profile, const std::filesystem::path & dest, bool notifySystem);

} // namespace nix::darwin::detail
