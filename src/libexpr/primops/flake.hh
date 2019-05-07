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
    struct FlakeEntry
    {
        FlakeRef ref;
        std::map<FlakeRef, FlakeEntry> flakeEntries;
        std::map<FlakeAlias, FlakeRef> nonFlakeEntries;
        FlakeEntry(const FlakeRef & flakeRef) : ref(flakeRef) {};
    };

    std::map<FlakeRef, FlakeEntry> flakeEntries;
    std::map<FlakeId, FlakeRef> nonFlakeEntries;
};

typedef std::vector<std::shared_ptr<FlakeRegistry>> Registries;

Path getUserRegistryPath();

enum RegistryAccess { DisallowRegistry, AllowRegistry, AllowRegistryAtTop };

void makeFlakeValue(EvalState & state, const FlakeRef & flakeRef, RegistryAccess registryAccess, Value & v);

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
    Path storePath;
    // date
    // content hash
    NonFlake(const FlakeRef & origRef, const SourceInfo & sourceInfo) : originalRef(origRef),
        resolvedRef(sourceInfo.resolvedRef), revCount(sourceInfo.revCount), storePath(sourceInfo.storePath) {};
};

std::shared_ptr<FlakeRegistry> getGlobalRegistry();

Flake getFlake(EvalState &, const FlakeRef &, bool impureIsAllowed);

struct ResolvedFlake
{
    Flake flake;
    std::vector<ResolvedFlake> flakeDeps; // The flake dependencies
    std::vector<NonFlake> nonFlakeDeps;
    ResolvedFlake(const Flake & flake) : flake(flake) {}
};

ResolvedFlake resolveFlake(EvalState &, const FlakeRef &, RegistryAccess registryAccess, bool isTopFlake = true);

void updateLockFile(EvalState &, const FlakeUri &);

void gitCloneFlake (std::string flakeUri, EvalState &, Registries, Path);
}
