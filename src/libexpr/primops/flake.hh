#include "types.hh"
#include "flakeref.hh"

#include <variant>

namespace nix {

static const size_t FLAG_REGISTRY = 0;
static const size_t USER_REGISTRY = 1;
static const size_t GLOBAL_REGISTRY = 2;

struct Value;
class EvalState;

struct FlakeRegistry
{
    std::map<FlakeRef, FlakeRef> entries;
};

struct LockFile
{
    struct NonFlakeEntry
    {
        FlakeRef ref;
        Hash contentHash;
        NonFlakeEntry(const FlakeRef & flakeRef, const Hash & hash) : ref(flakeRef), contentHash(hash) {};
    };

    struct FlakeEntry
    {
        FlakeRef ref;
        Hash contentHash;
        std::map<FlakeRef, FlakeEntry> flakeEntries;
        std::map<FlakeAlias, NonFlakeEntry> nonFlakeEntries;
        FlakeEntry(const FlakeRef & flakeRef, const Hash & hash) : ref(flakeRef), contentHash(hash) {};
    };

    std::map<FlakeRef, FlakeEntry> flakeEntries;
    std::map<FlakeAlias, NonFlakeEntry> nonFlakeEntries;
};

typedef std::vector<std::shared_ptr<FlakeRegistry>> Registries;

Path getUserRegistryPath();

enum HandleLockFile
    { AllPure // Everything is handled 100% purely
    , TopRefUsesRegistries // The top FlakeRef uses the registries, apart from that, everything happens 100% purely
    , UpdateLockFile // Update the existing lockfile and write it to file
    , UseUpdatedLockFile // `UpdateLockFile` without writing to file
    , RecreateLockFile // Recreate the lockfile from scratch and write it to file
    , UseNewLockFile // `RecreateLockFile` without writing to file
    };

void makeFlakeValue(EvalState &, const FlakeRef &, HandleLockFile, Value &);

std::shared_ptr<FlakeRegistry> readRegistry(const Path &);

void writeRegistry(const FlakeRegistry &, const Path &);

struct SourceInfo
{
    FlakeRef resolvedRef;
    Path storePath;
    std::optional<uint64_t> revCount;
    // date
    SourceInfo(const FlakeRef & resolvRef) : resolvedRef(resolvRef) {};
};

struct Flake
{
    FlakeId id;
    FlakeRef originalRef;
    FlakeRef resolvedRef;
    std::string description;
    std::optional<uint64_t> revCount;
    Path storePath;
    Hash hash; // content hash
    std::vector<FlakeRef> requires;
    std::map<FlakeAlias, FlakeRef> nonFlakeRequires;
    Value * vProvides; // FIXME: gc
    // date
    // content hash
    Flake(const FlakeRef & origRef, const SourceInfo & sourceInfo) : originalRef(origRef),
        resolvedRef(sourceInfo.resolvedRef), revCount(sourceInfo.revCount), storePath(sourceInfo.storePath) {};
};

struct NonFlake
{
    FlakeAlias alias;
    FlakeRef originalRef;
    FlakeRef resolvedRef;
    std::optional<uint64_t> revCount;
    Hash hash;
    Path storePath;
    // date
    NonFlake(const FlakeRef & origRef, const SourceInfo & sourceInfo) : originalRef(origRef),
        resolvedRef(sourceInfo.resolvedRef), revCount(sourceInfo.revCount), storePath(sourceInfo.storePath) {};
};

std::shared_ptr<FlakeRegistry> getGlobalRegistry();

Flake getFlake(EvalState &, const FlakeRef &, bool impureIsAllowed);

struct ResolvedFlake
{
    Flake flake;
    std::map<FlakeRef, ResolvedFlake> flakeDeps; // The key in this map, is the originalRef as written in flake.nix
    std::vector<NonFlake> nonFlakeDeps;
    ResolvedFlake(const Flake & flake) : flake(flake) {}
};

ResolvedFlake resolveFlake(EvalState &, const FlakeRef &, HandleLockFile);

void updateLockFile(EvalState &, const FlakeUri &, bool recreateLockFile);

void gitCloneFlake (std::string flakeUri, EvalState &, Registries, Path);
}
