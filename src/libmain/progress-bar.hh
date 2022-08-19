#pragma once

#include "logging.hh"

namespace nix {

std::unique_ptr<Logger> makeProgressBar(bool printBuildLogs = false);

void startProgressBar(bool printBuildLogs = false);

void stopProgressBar();

}
