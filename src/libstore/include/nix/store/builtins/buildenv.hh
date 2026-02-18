#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

/**
 * Think of this as a "store level package attrset", but stripped down to no more than the needs of buildenv.
 */
struct Package
{
    Path path;
    bool active;
    int priority;

    Package(const Path & path, bool active, int priority)
        : path{path}
        , active{active}
        , priority{priority}
    {
    }
};

class BuildEnvFileConflictError final : public CloneableError<BuildEnvFileConflictError, Error>
{
public:
    const Path fileA;
    const Path fileB;
    int priority;

    BuildEnvFileConflictError(const Path fileA, const Path fileB, int priority)
        : CloneableError(
              "Unable to build profile. There is a conflict for the following files:\n"
              "\n"
              "  %1%\n"
              "  %2%",
              fileA,
              fileB)
        , fileA(fileA)
        , fileB(fileB)
        , priority(priority)
    {
    }
};

typedef std::vector<Package> Packages;

void buildProfile(const Path & out, Packages && pkgs);

} // namespace nix
