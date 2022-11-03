#pragma once

#include "logging.hh"

namespace nix {

Logger * makeProgressBar();

void startProgressBar();

void stopProgressBar();

}
