#pragma once

#include "types.hh"
#include "flakeref.hh"
#include "lockfile.hh"

namespace nix {

struct Value;
class EvalState;

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
    , TopRefMutable // The top FlakeRef can be mutable, the rest is pure
    , RecreateLockFile // Recreate lockfile from scratch
    , UpdateLockFile // Update lockfile
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
    FlakeRef originalRef;
    SourceInfo sourceInfo;
    NonFlake(const FlakeRef & origRef, const SourceInfo & sourceInfo)
        : originalRef(origRef), sourceInfo(sourceInfo) {};
};

Flake getFlake(EvalState &, const FlakeRef &, bool impureIsAllowed);

/* Fingerprint of a locked flake; used as a cache key. */
typedef Hash Fingerprint;

struct ResolvedFlake
{
    Flake flake;
    LockFile lockFile;

    ResolvedFlake(Flake && flake, LockFile && lockFile)
        : flake(flake), lockFile(lockFile) {}

    Fingerprint getFingerprint() const;
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
