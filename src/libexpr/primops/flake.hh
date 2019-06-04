#pragma once

#include "types.hh"
#include "flakeref.hh"

#include <variant>
#include <nlohmann/json.hpp>

namespace nix {

struct Value;
class EvalState;
class Store;

namespace flake {

static const size_t FLAG_REGISTRY = 0;
static const size_t USER_REGISTRY = 1;
static const size_t GLOBAL_REGISTRY = 2;

struct FlakeRegistry
{
    std::map<FlakeRef, FlakeRef> entries;
};

typedef std::vector<std::shared_ptr<FlakeRegistry>> Registries;

std::shared_ptr<FlakeRegistry> readRegistry(const Path &);

void writeRegistry(const FlakeRegistry &, const Path &);

Path getUserRegistryPath();

enum HandleLockFile : unsigned int
    { AllPure // Everything is handled 100% purely
    , TopRefUsesRegistries // The top FlakeRef uses the registries, apart from that, everything happens 100% purely
    , UpdateLockFile // Update the existing lockfile and write it to file
    , UseUpdatedLockFile // `UpdateLockFile` without writing to file
    , RecreateLockFile // Recreate the lockfile from scratch and write it to file
    , UseNewLockFile // `RecreateLockFile` without writing to file
    };

struct AbstractDep
{
    FlakeRef ref;
    Hash narHash;

    AbstractDep(const FlakeRef & flakeRef, const Hash & narHash)
        : ref(flakeRef), narHash(narHash) {};

    AbstractDep(const nlohmann::json & json);

    nlohmann::json toJson() const;

    Path computeStorePath(Store & store) const;
};

struct NonFlakeDep : AbstractDep
{
    using AbstractDep::AbstractDep;

    bool operator ==(const NonFlakeDep & other) const
    {
        return ref == other.ref && narHash == other.narHash;
    }
};

struct FlakeDep;

struct FlakeInputs
{
    std::map<FlakeRef, FlakeDep> flakeDeps;
    std::map<FlakeAlias, NonFlakeDep> nonFlakeDeps;

    FlakeInputs() {};
    FlakeInputs(const nlohmann::json & json);

    nlohmann::json toJson() const;
};

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
};

struct SourceInfo
{
    // Immutable flakeref that this source tree was obtained from.
    FlakeRef resolvedRef;

    Path storePath;

    // Number of ancestors of the most recent commit.
    std::optional<uint64_t> revCount;

    // NAR hash of the store path.
    Hash narHash;

    // A stable timestamp of this source tree. For Git and GitHub
    // flakes, the commit date (not author date!) of the most recent
    // commit.
    std::optional<time_t> lastModified;

    SourceInfo(const FlakeRef & resolvRef) : resolvedRef(resolvRef) {};
};

struct Flake
{
    FlakeId id;
    FlakeRef originalRef;
    std::string description;
    SourceInfo sourceInfo;
    std::vector<FlakeRef> inputs;
    std::map<FlakeAlias, FlakeRef> nonFlakeInputs;
    Value * vOutputs; // FIXME: gc
    unsigned int epoch;

    Flake(const FlakeRef & origRef, const SourceInfo & sourceInfo)
        : originalRef(origRef), sourceInfo(sourceInfo) {};
};

struct NonFlake
{
    FlakeAlias alias;
    FlakeRef originalRef;
    SourceInfo sourceInfo;
    NonFlake(const FlakeRef & origRef, const SourceInfo & sourceInfo)
        : originalRef(origRef), sourceInfo(sourceInfo) {};
};

Flake getFlake(EvalState &, const FlakeRef &, bool impureIsAllowed);

struct ResolvedFlake
{
    Flake flake;
    LockFile lockFile;
    ResolvedFlake(Flake && flake, LockFile && lockFile)
        : flake(flake), lockFile(lockFile) {}
};

ResolvedFlake resolveFlake(EvalState &, const FlakeRef &, HandleLockFile);

void callFlake(EvalState & state,
    const Flake & flake,
    const FlakeInputs & inputs,
    Value & v);

void callFlake(EvalState & state,
    const ResolvedFlake & resFlake,
    Value & v);

void updateLockFile(EvalState &, const FlakeRef & flakeRef, bool recreateLockFile);

void gitCloneFlake(FlakeRef flakeRef, EvalState &, Registries, const Path & destDir);

}

}
