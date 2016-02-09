#pragma once

#include "args.hh"

namespace nix {

struct MixCommonArgs : virtual Args
{
    MixCommonArgs(const string & programName);
};

struct MixDryRun : virtual Args
{
    bool dryRun;

    MixDryRun()
    {
        mkFlag(0, "dry-run", "show what this command would do without doing it", &dryRun);
    }
};

}
