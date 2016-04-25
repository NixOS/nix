#pragma once

#include "logging.hh"

namespace nix {

class StartProgressBar
{
    Logger * prev = 0;
public:
    StartProgressBar();
    ~StartProgressBar();
};

}
