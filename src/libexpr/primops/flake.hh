#include "types.hh"
#include "flakeref.hh"

#include <variant>

namespace nix {

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
        std::map<FlakeId, FlakeRef> nonFlakeEntries;
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

struct FlakeSourceInfo
{
    FlakeRef flakeRef;
    Path storePath;
    std::optional<Hash> rev;
    std::optional<uint64_t> revCount;
    // date
    FlakeSourceInfo(const FlakeRef & flakeRef) : flakeRef(flakeRef) { }
};

struct Flake
{
    FlakeId id;
    FlakeRef ref;
    std::string description;
    FlakeSourceInfo sourceInfo;
    std::vector<FlakeRef> requires;
    std::map<FlakeAlias, FlakeRef> nonFlakeRequires;
    Value * vProvides; // FIXME: gc
    Flake(const FlakeRef & flakeRef, FlakeSourceInfo && sourceInfo)
        : ref(flakeRef), sourceInfo(sourceInfo) {};
};

struct NonFlake
{
    FlakeAlias alias;
    FlakeRef ref;
    Path path;
    // date
    // content hash
    NonFlake(const FlakeRef flakeRef) : ref(flakeRef) {};
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

void updateLockFile(EvalState &, const Path & path);

void gitCloneFlake (std::string flakeUri, EvalState &, Registries, Path);
}
