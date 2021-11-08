#pragma once

#include "logging.hh"

namespace nix {

Logger * makeProgressBar();

void stopProgressBar();

struct ProgressBarSettings : Config
{
    Setting<bool> printBuildLogs{this, false, "print-build-logs",
        "Whether the progress bar should print full build logs or just the most recent line."};
};

extern ProgressBarSettings progressBarSettings;

}
