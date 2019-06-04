#pragma once

#include "flakeref.hh"

#include <nlohmann/json.hpp>

namespace nix {
class Store;
}

namespace nix::flake {

/* Common lock file information about a flake input, namely the
   immutable ref and the NAR hash. */
struct AbstractDep
{
    FlakeRef ref;
    Hash narHash;

    AbstractDep(const FlakeRef & flakeRef, const Hash & narHash)
        : ref(flakeRef), narHash(narHash)
    {
        assert(ref.isImmutable());
    };

    AbstractDep(const nlohmann::json & json);

    nlohmann::json toJson() const;

    Path computeStorePath(Store & store) const;
};

/* Lock file information about a non-flake input. */
struct NonFlakeDep : AbstractDep
{
    using AbstractDep::AbstractDep;

    bool operator ==(const NonFlakeDep & other) const
    {
        return ref == other.ref && narHash == other.narHash;
    }
};

struct FlakeDep;

/* Lock file information about the dependencies of a flake. */
struct FlakeInputs
{
    std::map<FlakeRef, FlakeDep> flakeDeps;
    std::map<FlakeAlias, NonFlakeDep> nonFlakeDeps;

    FlakeInputs() {};
    FlakeInputs(const nlohmann::json & json);

    nlohmann::json toJson() const;
};

/* Lock file information about a flake input. */
struct FlakeDep : FlakeInputs, AbstractDep
{
    FlakeId id;

    FlakeDep(const FlakeId & id, const FlakeRef & flakeRef, const Hash & narHash)
        : AbstractDep(flakeRef, narHash), id(id) {};

    FlakeDep(const nlohmann::json & json);

    bool operator ==(const FlakeDep & other) const
    {
        return
            id == other.id
            && ref == other.ref
            && narHash == other.narHash
            && flakeDeps == other.flakeDeps
            && nonFlakeDeps == other.nonFlakeDeps;
    }

    nlohmann::json toJson() const;
};

/* An entire lock file. Note that this cannot be a FlakeDep for the
   top-level flake, because then the lock file would need to contain
   the hash of the top-level flake, but committing the lock file
   would invalidate that hash. */
struct LockFile : FlakeInputs
{
    bool operator ==(const LockFile & other) const
    {
        return
            flakeDeps == other.flakeDeps
            && nonFlakeDeps == other.nonFlakeDeps;
    }

    LockFile() {}
    LockFile(const nlohmann::json & json) : FlakeInputs(json) {}
    LockFile(FlakeDep && dep)
    {
        flakeDeps = std::move(dep.flakeDeps);
        nonFlakeDeps = std::move(dep.nonFlakeDeps);
    }

    nlohmann::json toJson() const;

    static LockFile read(const Path & path);

    void write(const Path & path) const;
};

}

