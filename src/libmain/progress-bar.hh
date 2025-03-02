#pragma once
///@file

#include "logging.hh"

namespace nix {

std::unique_ptr<Logger> makeProgressBar();

}
