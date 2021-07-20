#pragma once

#include "logging.hh"

namespace nix {

ref<Logger> makeProgressBar(bool printBuildLogs = false);

void startProgressBar(bool printBuildLogs = false);

void stopProgressBar();

}
