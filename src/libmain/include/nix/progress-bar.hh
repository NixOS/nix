#pragma once
///@file

#include "nix/logging.hh"

namespace nix {

std::unique_ptr<Logger> makeProgressBar();

}
