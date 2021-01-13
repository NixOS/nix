#pragma once

#include "args.hh"

namespace nix {

struct MixCommonArgs : virtual Args
{
    string programName;
    MixCommonArgs(const string & programName);
};

struct MixDryRun : virtual Args
{
    bool dryRun = false;

    MixDryRun()
    {
        mkFlag(0, "dry-run", "Show what this command would do without doing it.", &dryRun);
    }
};

struct MixJSON : virtual Args
{
    bool json = false;

    MixJSON()
    {
        mkFlag(0, "json", "Produce output in JSON format, suitable for consumption by another program.", &json);
    }
};

}
