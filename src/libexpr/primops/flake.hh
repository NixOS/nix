#include "types.hh"
#include "flakeref.hh"

#include <variant>

namespace nix {

struct Value;
class EvalState;

struct FlakeRegistry
{
    struct Entry
    {
        FlakeRef ref;
        Entry(const FlakeRef & flakeRef) : ref(flakeRef) {};
        Entry operator=(const Entry & entry) { return Entry(entry.ref); }
    };
    std::map<FlakeId, Entry> entries;
};

Path getUserRegistryPath();

Value * makeFlakeRegistryValue(EvalState & state);

Value * makeFlakeValue(EvalState & state, const FlakeRef & flakeRef, bool impureTopRef, Value & v);

std::shared_ptr<FlakeRegistry> readRegistry(const Path &);

void writeRegistry(FlakeRegistry, Path);

struct Flake
{
    FlakeId id;
    FlakeRef ref;
    std::string description;
    Path path;
    std::optional<uint64_t> revCount;
    std::vector<FlakeRef> requires;
    std::shared_ptr<FlakeRegistry> lockFile;
    std::map<FlakeId, FlakeRef> nonFlakeRequires;
    Value * vProvides; // FIXME: gc
    // date
    // content hash
    Flake(const FlakeRef flakeRef) : ref(flakeRef) {};
};

struct NonFlake
{
    FlakeId id;
    FlakeRef ref;
    Path path;
    // date
    // content hash
    NonFlake(const FlakeRef flakeRef) : ref(flakeRef) {};
};

Flake getFlake(EvalState &, const FlakeRef &);

struct Dependencies
{
    FlakeId topFlakeId;
    std::vector<Flake> flakes;
    std::vector<NonFlake> nonFlakes;
};

Dependencies resolveFlake(EvalState &, const FlakeRef &, bool impureTopRef);

FlakeRegistry updateLockFile(EvalState &, Flake &);

void updateLockFile(EvalState &, Path);
}
