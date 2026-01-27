#pragma once
///@file

#include "nix/store/store-api.hh"
#include "nix/util/fmt.hh"

namespace nix {

/**
 * Think of this as a "store level package attrset", but stripped down to no more than the needs of buildenv.
 */
struct Package
{
    std::filesystem::path path;
    bool active;
    int priority;

    Package(const std::filesystem::path & path, bool active, int priority)
        : path{path}
        , active{active}
        , priority{priority}
    {
    }
};

class BuildEnvFileConflictError final : public CloneableError<BuildEnvFileConflictError, Error>
{
public:
    const std::filesystem::path fileA;
    const std::filesystem::path fileB;
    int priority;

    BuildEnvFileConflictError(const std::filesystem::path fileA, const std::filesystem::path fileB, int priority)
        : CloneableError(
              "Unable to build profile. There is a conflict for the following files:\n"
              "\n"
              "  %1%\n"
              "  %2%",
              PathFmt(fileA),
              PathFmt(fileB))
        , fileA(fileA)
        , fileB(fileB)
        , priority(priority)
    {
    }
};

typedef std::vector<Package> Packages;

void buildProfile(const std::filesystem::path & out, Packages && pkgs);

} // namespace nix
