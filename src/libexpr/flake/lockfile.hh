#pragma once

#include "flakeref.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {
class Store;
}

namespace nix::flake {

struct LockedInput;

/* Lock file information about the dependencies of a flake. */
struct LockedInputs
{
    std::map<FlakeId, LockedInput> inputs;

    LockedInputs() {};
    LockedInputs(const nlohmann::json & json);

    nlohmann::json toJson() const;

    bool isImmutable() const;
};

/* Lock file information about a flake input. */
struct LockedInput : LockedInputs
{
    FlakeRef ref, originalRef;
    Hash narHash;

    LockedInput(const FlakeRef & ref, const FlakeRef & originalRef, const Hash & narHash)
        : ref(ref), originalRef(originalRef), narHash(narHash)
    { }

    LockedInput(const nlohmann::json & json);

    bool operator ==(const LockedInput & other) const
    {
        return
            ref == other.ref
            && narHash == other.narHash
            && inputs == other.inputs;
    }

    nlohmann::json toJson() const;

    Path computeStorePath(Store & store) const;
};

/* An entire lock file. Note that this cannot be a FlakeInput for the
   top-level flake, because then the lock file would need to contain
   the hash of the top-level flake, but committing the lock file
   would invalidate that hash. */
struct LockFile : LockedInputs
{
    bool operator ==(const LockFile & other) const
    {
        return inputs == other.inputs;
    }

    LockFile() {}
    LockFile(const nlohmann::json & json) : LockedInputs(json) {}
    LockFile(LockedInput && dep)
    {
        inputs = std::move(dep.inputs);
    }

    nlohmann::json toJson() const;

    static LockFile read(const Path & path);

    void write(const Path & path) const;
};

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile);

}

