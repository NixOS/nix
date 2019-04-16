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
        std::map<FlakeId, FlakeEntry> flakeEntries;
        std::map<FlakeId, FlakeRef> nonFlakeEntries;
        FlakeEntry(const FlakeRef & flakeRef) : ref(flakeRef) {};
    };

    std::map<FlakeId, FlakeEntry> flakeEntries;
    std::map<FlakeId, FlakeRef> nonFlakeEntries;
};

Path getUserRegistryPath();

enum RegistryAccess { DisallowRegistry, AllowRegistry, AllowRegistryAtTop };

void makeFlakeValue(EvalState & state, const FlakeRef & flakeRef, RegistryAccess registryAccess, Value & v);

std::shared_ptr<FlakeRegistry> readRegistry(const Path &);

void writeRegistry(const FlakeRegistry &, const Path &);

struct Flake
{
    FlakeId id;
    FlakeRef ref;
    std::string description;
    Path path;
    std::optional<uint64_t> revCount;
    std::vector<FlakeRef> requires;
    LockFile lockFile;
    std::map<FlakeAlias, FlakeRef> nonFlakeRequires;
    Value * vProvides; // FIXME: gc
    // date
    // content hash
    Flake(const FlakeRef flakeRef) : ref(flakeRef) {};
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

struct Dependencies
{
    Flake flake;
    std::vector<Dependencies> flakeDeps; // The flake dependencies
    std::vector<NonFlake> nonFlakeDeps;
    Dependencies(const Flake & flake) : flake(flake) {}
};

Dependencies resolveFlake(EvalState &, const FlakeRef &, RegistryAccess registryAccess, bool isTopFlake = true);

void updateLockFile(EvalState &, const Path & path);

}
