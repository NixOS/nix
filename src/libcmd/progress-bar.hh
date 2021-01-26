#pragma once

#include "logging.hh"

namespace nix {

void startProgressBar(bool printBuildLogs = false);

void stopProgressBar();

}
