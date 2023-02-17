#pragma once

#include "nix/util/logging.hh"

namespace nix {

Logger * makeProgressBar();

void startProgressBar();

void stopProgressBar();

}
