#pragma once

#include "types.hh"
#include "flakeref.hh"
#include "lockfile.hh"

namespace nix {

struct Value;
class EvalState;

namespace fetchers { struct Tree; }

namespace flake {

enum LockFileMode : unsigned int
    { AllPure // Everything is handled 100% purely
    , TopRefUsesRegistries // The top FlakeRef uses the registries, apart from that, everything happens 100% purely
    , UpdateLockFile // Update the existing lockfile and write it to file
    , UseUpdatedLockFile // `UpdateLockFile` without writing to file
    , RecreateLockFile // Recreate the lockfile from scratch and write it to file
    , UseNewLockFile // `RecreateLockFile` without writing to file
    };

struct FlakeInput;

typedef std::map<FlakeId, FlakeInput> FlakeInputs;

struct FlakeInput
{
    FlakeRef ref;
    bool isFlake = true;
    std::optional<InputPath> follows;
    FlakeInputs overrides;
};

struct Flake
{
    FlakeRef originalRef;
    FlakeRef resolvedRef;
    std::optional<std::string> description;
    std::shared_ptr<const fetchers::Tree> sourceInfo;
    FlakeInputs inputs;
    Value * vOutputs; // FIXME: gc
    unsigned int edition;
    ~Flake();
};

Flake getFlake(EvalState & state, const FlakeRef & flakeRef, bool allowLookup);

/* Fingerprint of a locked flake; used as a cache key. */
typedef Hash Fingerprint;

struct LockedFlake
{
    Flake flake;
    LockFile lockFile;

    Fingerprint getFingerprint() const;
};

struct LockFlags
{
    std::map<InputPath, FlakeRef> inputOverrides;
};

LockedFlake lockFlake(
    EvalState &,
    const FlakeRef &,
    LockFileMode,
    const LockFlags &);

void callFlake(EvalState & state,
    const Flake & flake,
    const LockedInputs & inputs,
    Value & v);

void callFlake(EvalState & state,
    const LockedFlake & resFlake,
    Value & v);

}

}
