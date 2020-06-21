#pragma once

#include "logging.hh"

namespace nix {

Logger * makeProgressBar(bool printBuildLogs = false);

void startProgressBar(bool printBuildLogs = false);

void stopProgressBar();

}
