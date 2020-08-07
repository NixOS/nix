#pragma once

#include "types.hh"
#include "flakeref.hh"
#include "lockfile.hh"
#include "value.hh"

namespace nix {

class EvalState;

namespace fetchers { struct Tree; }

namespace flake {

struct FlakeInput;

typedef std::map<FlakeId, FlakeInput> FlakeInputs;

struct FlakeInput
{
    std::optional<FlakeRef> ref;
    bool isFlake = true;
    std::optional<InputPath> follows;
    bool absolute = false; // whether 'follows' is relative to the flake root
    FlakeInputs overrides;
};

struct Flake
{
    FlakeRef originalRef;
    FlakeRef resolvedRef;
    FlakeRef lockedRef;
    std::optional<std::string> description;
    std::shared_ptr<const fetchers::Tree> sourceInfo;
    FlakeInputs inputs;
    RootValue vOutputs;
    ~Flake();
};

Flake getFlake(EvalState & state, const FlakeRef & flakeRef, bool allowLookup);

/* Fingerprint of a locked flake; used as a cache key. */
typedef Hash Fingerprint;

struct LockedFlake
{
    Flake flake;
    LockFile lockFile;

    Fingerprint getFingerprint(const Store & store) const;
};

struct LockFlags
{
    /* Whether to ignore the existing lock file, creating a new one
       from scratch. */
    bool recreateLockFile = false;

    /* Whether to update the lock file at all. If set to false, if any
       change to the lock file is needed (e.g. when an input has been
       added to flake.nix), you get a fatal error. */
    bool updateLockFile = true;

    /* Whether to write the lock file to disk. If set to true, if the
       any changes to the lock file are needed and the flake is not
       writable (i.e. is not a local Git working tree or similar), you
       get a fatal error. If set to false, Nix will use the modified
       lock file in memory only, without writing it to disk. */
    bool writeLockFile = true;

    /* Whether to use the registries to lookup indirect flake
       references like 'nixpkgs'. */
    bool useRegistries = true;

    /* Whether mutable flake references (i.e. those without a Git
       revision or similar) without a corresponding lock are
       allowed. Mutable flake references with a lock are always
       allowed. */
    bool allowMutable = true;

    /* Whether to commit changes to flake.lock. */
    bool commitLockFile = false;

    /* Flake inputs to be overriden. */
    std::map<InputPath, FlakeRef> inputOverrides;

    /* Flake inputs to be updated. This means that any existing lock
       for those inputs will be ignored. */
    std::set<InputPath> inputUpdates;
};

LockedFlake lockFlake(
    EvalState & state,
    const FlakeRef & flakeRef,
    const LockFlags & lockFlags);

void callFlake(
    EvalState & state,
    const LockedFlake & lockedFlake,
    Value & v);

}

void emitTreeAttrs(
    EvalState & state,
    const fetchers::Tree & tree,
    const fetchers::Input & input,
    Value & v, bool emptyRevFallback = false);

}
