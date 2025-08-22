#pragma once
///@file

#include "nix/store/store-api.hh"

namespace nix {

/**
 * Mix-in class for stores that can terminate active builds.
 */
struct BuildControlStore : public virtual Store
{
private:
    void anchor() override;

public:
    inline static std::string operationName = "Build termination";

    /**
     * Ask the owner of the output locks associated with `path` to terminate.
     * `path` must be an output path or registered derivation.
     *
     * @return The process ID signalled, or `std::nullopt` if no associated lock is held.
     */
    virtual std::optional<uint64_t> killBuild(const StorePath & path) = 0;
};

} // namespace nix
