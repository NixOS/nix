#pragma once
/**
 * @file Misc type definitions for both local building and remote (RPC building)
 */

#include "nix/util/hash.hh"
#include "nix/store/path.hh"

namespace nix {

class Store;
struct Derivation;

/**
 * Unless we are repairing, we don't both to test validity and just assume it,
 * so the choices are `Absent` or `Valid`.
 */
enum struct PathStatus {
    Corrupt,
    Absent,
    Valid,
};

struct InitialOutputStatus
{
    StorePath path;
    PathStatus status;

    /**
     * Valid in the store, and additionally non-corrupt if we are repairing
     */
    bool isValid() const
    {
        return status == PathStatus::Valid;
    }

    /**
     * Merely present, allowed to be corrupt
     */
    bool isPresent() const
    {
        return status == PathStatus::Corrupt || status == PathStatus::Valid;
    }
};

struct InitialOutput
{
    bool wanted;
    Hash outputHash;
    std::optional<InitialOutputStatus> known;
};

void runPostBuildHook(Store & store, Logger & logger, const StorePath & drvPath, const StorePathSet & outputPaths);

/**
 * Format the known outputs of a derivation for use in error messages.
 */
std::string showKnownOutputs(Store & store, const Derivation & drv);

} // namespace nix
