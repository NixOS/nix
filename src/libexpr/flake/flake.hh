#pragma once
///@file

#include "types.hh"
#include "flakeref.hh"
#include "lockfile.hh"
#include "value.hh"

namespace nix {

class EvalState;

namespace flake {

struct FlakeInput;

typedef std::map<FlakeId, FlakeInput> FlakeInputs;

/**
 * FlakeInput is the 'Flake'-level parsed form of the "input" entries
 * in the flake file.
 *
 * A FlakeInput is normally constructed by the 'parseFlakeInput'
 * function which parses the input specification in the '.flake' file
 * to create a 'FlakeRef' (a fetcher, the fetcher-specific
 * representation of the input specification, and possibly the fetched
 * local store path result) and then creating this FlakeInput to hold
 * that FlakeRef, along with anything that might override that
 * FlakeRef (like command-line overrides or "follows" specifications).
 *
 * A FlakeInput is also sometimes constructed directly from a FlakeRef
 * instead of starting at the flake-file input specification
 * (e.g. overrides, follows, and implicit inputs).
 *
 * A FlakeInput will usually have one of either "ref" or "follows"
 * set.  If not otherwise specified, a "ref" will be generated to a
 * 'type="indirect"' flake, which is treated as simply the name of a
 * flake to be resolved in the registry.
 */

struct FlakeInput
{
    std::optional<FlakeRef> ref;
    /**
     * true = process flake to get outputs
     *
     * false = (fetched) static source path
     */
    bool isFlake = true;
    std::optional<InputPath> follows;
    FlakeInputs overrides;
};

struct ConfigFile
{
    using ConfigValue = std::variant<std::string, int64_t, Explicit<bool>, std::vector<std::string>>;

    std::map<std::string, ConfigValue> settings;

    void apply();
};

/**
 * The contents of a flake.nix file.
 */
struct Flake
{
    /**
     * The original flake specification (by the user)
     */
    FlakeRef originalRef;
    /**
     * registry references and caching resolved to the specific underlying flake
     */
    FlakeRef resolvedRef;
    /**
     * the specific local store result of invoking the fetcher
     */
    FlakeRef lockedRef;
    /**
     * The path of `flake.nix`.
     */
    SourcePath path;
    /**
     * pretend that 'lockedRef' is dirty
     */
    bool forceDirty = false;
    std::optional<std::string> description;
    FlakeInputs inputs;
    /**
     * 'nixConfig' attribute
     */
    ConfigFile config;

    ~Flake();

    SourcePath lockFilePath()
    {
        return path.parent() / "flake.lock";
    }
};

Flake getFlake(EvalState & state, const FlakeRef & flakeRef, bool allowLookup);

/**
 * Fingerprint of a locked flake; used as a cache key.
 */
typedef Hash Fingerprint;

struct LockedFlake
{
    Flake flake;
    LockFile lockFile;

    /**
     * Source tree accessors for nodes that have been fetched in
     * lockFlake(); in particular, the root node and the overriden
     * inputs.
     */
    std::map<ref<Node>, SourcePath> nodePaths;

    std::optional<Fingerprint> getFingerprint(ref<Store> store) const;
};

struct LockFlags
{
    /**
     * Whether to ignore the existing lock file, creating a new one
     * from scratch.
     */
    bool recreateLockFile = false;

    /**
     * Whether to update the lock file at all. If set to false, if any
     * change to the lock file is needed (e.g. when an input has been
     * added to flake.nix), you get a fatal error.
     */
    bool updateLockFile = true;

    /**
     * Whether to write the lock file to disk. If set to true, if the
     * any changes to the lock file are needed and the flake is not
     * writable (i.e. is not a local Git working tree or similar), you
     * get a fatal error. If set to false, Nix will use the modified
     * lock file in memory only, without writing it to disk.
     */
    bool writeLockFile = true;

    /**
     * Whether to use the registries to lookup indirect flake
     * references like 'nixpkgs'.
     */
    std::optional<bool> useRegistries = std::nullopt;

    /**
     * Whether to apply flake's nixConfig attribute to the configuration
     */

    bool applyNixConfig = false;

    /**
     * Whether unlocked flake references (i.e. those without a Git
     * revision or similar) without a corresponding lock are
     * allowed. Unlocked flake references with a lock are always
     * allowed.
     */
    bool allowUnlocked = true;

    /**
     * Whether to commit changes to flake.lock.
     */
    bool commitLockFile = false;

    /**
     * The path to a lock file to read instead of the `flake.lock` file in the top-level flake
     */
    std::optional<SourcePath> referenceLockFilePath;

    /**
     * The path to a lock file to write to instead of the `flake.lock` file in the top-level flake
     */
    std::optional<Path> outputLockFilePath;

    /**
     * Flake inputs to be overridden.
     */
    std::map<InputPath, FlakeRef> inputOverrides;

    /**
     * Flake inputs to be updated. This means that any existing lock
     * for those inputs will be ignored.
     */
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
    const StorePath & storePath,
    const fetchers::Input & input,
    Value & v,
    bool emptyRevFallback = false,
    bool forceDirty = false);

}
