#pragma once
///@file

#include <memory>

namespace nix {

class Logger;

std::unique_ptr<Logger> makeProgressBar();

} // namespace nix
