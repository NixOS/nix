#pragma once

#include "flakeref.hh"

#include <nlohmann/json.hpp>

namespace nix {
class Store;
}

namespace nix::flake {

/* Common lock file information about a flake input, namely the
   immutable ref and the NAR hash. */
struct AbstractInput
{
    FlakeRef ref;
    Hash narHash;

    AbstractInput(const FlakeRef & flakeRef, const Hash & narHash)
        : ref(flakeRef), narHash(narHash)
    {
        assert(ref.isImmutable());
    };

    AbstractInput(const nlohmann::json & json);

    nlohmann::json toJson() const;

    Path computeStorePath(Store & store) const;
};

/* Lock file information about a non-flake input. */
struct NonFlakeInput : AbstractInput
{
    using AbstractInput::AbstractInput;

    bool operator ==(const NonFlakeInput & other) const
    {
        return ref == other.ref && narHash == other.narHash;
    }
};

struct FlakeInput;

/* Lock file information about the dependencies of a flake. */
struct FlakeInputs
{
    std::map<FlakeRef, FlakeInput> flakeInputs;
    std::map<FlakeAlias, NonFlakeInput> nonFlakeInputs;

    FlakeInputs() {};
    FlakeInputs(const nlohmann::json & json);

    nlohmann::json toJson() const;
};

/* Lock file information about a flake input. */
struct FlakeInput : FlakeInputs, AbstractInput
{
    FlakeId id;

    FlakeInput(const FlakeId & id, const FlakeRef & flakeRef, const Hash & narHash)
        : AbstractInput(flakeRef, narHash), id(id) {};

    FlakeInput(const nlohmann::json & json);

    bool operator ==(const FlakeInput & other) const
    {
        return
            id == other.id
            && ref == other.ref
            && narHash == other.narHash
            && flakeInputs == other.flakeInputs
            && nonFlakeInputs == other.nonFlakeInputs;
    }

    nlohmann::json toJson() const;
};

/* An entire lock file. Note that this cannot be a FlakeInput for the
   top-level flake, because then the lock file would need to contain
   the hash of the top-level flake, but committing the lock file
   would invalidate that hash. */
struct LockFile : FlakeInputs
{
    bool operator ==(const LockFile & other) const
    {
        return
            flakeInputs == other.flakeInputs
            && nonFlakeInputs == other.nonFlakeInputs;
    }

    LockFile() {}
    LockFile(const nlohmann::json & json) : FlakeInputs(json) {}
    LockFile(FlakeInput && dep)
    {
        flakeInputs = std::move(dep.flakeInputs);
        nonFlakeInputs = std::move(dep.nonFlakeInputs);
    }

    nlohmann::json toJson() const;

    static LockFile read(const Path & path);

    void write(const Path & path) const;
};

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile);

}

