#pragma once
///@file

#include "nix/util/logging.hh"

namespace nix {

std::unique_ptr<Logger> makeProgressBar();

}
