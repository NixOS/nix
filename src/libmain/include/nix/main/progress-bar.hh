#pragma once
///@file

#include "nix/util/logging.hh"

namespace nix {

std::shared_ptr<Logger> makeProgressBar();

}
