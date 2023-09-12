#pragma once
///@file

#include "derivations.hh"
#include "store-api.hh"

namespace nix {

struct Package {
    Path path;
    bool active;
    int priority;
    Package(const Path & path, bool active, int priority) : path{path}, active{active}, priority{priority} {}
};

class BuildEnvFileConflictError : public Error
{
public:
    const Path fileA;
    const Path fileB;
    int priority;

    BuildEnvFileConflictError(
        const Path fileA,
        const Path fileB,
        int priority
    )
        : Error(
            "Unable to build profile. There is a conflict for the following files:\n"
            "\n"
            "  %1%\n"
            "  %2%",
            fileA,
            fileB
        )
        , fileA(fileA)
        , fileB(fileB)
        , priority(priority)
    {}
};

typedef std::vector<Package> Packages;

void buildProfile(const Path & out, Packages && pkgs);

void builtinBuildenv(const BasicDerivation & drv);

}
