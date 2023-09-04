#pragma once

#include "args.hh"

namespace nix {

//static constexpr auto commonArgsCategory = "Miscellaneous common options";
static constexpr auto loggingCategory = "Logging-related options";

class MixCommonArgs : public virtual Args
{
    void initialFlagsProcessed() override;
public:
    std::string programName;
    MixCommonArgs(const std::string & programName);
protected:
    virtual void pluginsInited() {}
};

struct MixDryRun : virtual Args
{
    bool dryRun = false;

    MixDryRun()
    {
        addFlag({
            .longName = "dry-run",
            .description = "Show what this command would do without doing it.",
            //.category = commonArgsCategory,
            .handler = {&dryRun, true},
        });
    }
};

struct MixJSON : virtual Args
{
    bool json = false;

    MixJSON()
    {
        addFlag({
            .longName = "json",
            .description = "Produce output in JSON format, suitable for consumption by another program.",
            //.category = commonArgsCategory,
            .handler = {&json, true},
        });
    }
};

}
