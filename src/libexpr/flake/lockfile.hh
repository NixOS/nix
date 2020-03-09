#pragma once

#include "flakeref.hh"

#include <nlohmann/json_fwd.hpp>

namespace nix {
class Store;
struct StorePath;
}

namespace nix::flake {

using namespace fetchers;

typedef std::vector<FlakeId> InputPath;

struct LockedInput;

/* Lock file information about the dependencies of a flake. */
struct LockedInputs
{
    std::map<FlakeId, LockedInput> inputs;

    LockedInputs() {};
    LockedInputs(const nlohmann::json & json);

    nlohmann::json toJson() const;

    bool isImmutable() const;

    std::optional<LockedInput *> findInput(const InputPath & path);

    void removeInput(const InputPath & path);
};

/* Lock file information about a flake input. */
struct LockedInput : LockedInputs
{
    FlakeRef lockedRef, originalRef;
    TreeInfo info;
    bool isFlake = true;

    LockedInput(
        const FlakeRef & lockedRef,
        const FlakeRef & originalRef,
        const TreeInfo & info,
        bool isFlake = true)
        : lockedRef(lockedRef), originalRef(originalRef), info(info), isFlake(isFlake)
    { }

    LockedInput(const nlohmann::json & json);

    bool operator ==(const LockedInput & other) const
    {
        return
            lockedRef == other.lockedRef
            && originalRef == other.originalRef
            && info == other.info
            && inputs == other.inputs
            && isFlake == other.isFlake;
    }

    nlohmann::json toJson() const;

    StorePath computeStorePath(Store & store) const;
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

InputPath parseInputPath(std::string_view s);

}

