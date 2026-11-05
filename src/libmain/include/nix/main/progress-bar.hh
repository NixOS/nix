#pragma once
///@file

#include "nix/util/logging.hh"

#include <memory>

namespace nix {

std::unique_ptr<Logger> makeProgressBar();

}
