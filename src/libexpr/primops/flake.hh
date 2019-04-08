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
    std::vector<FlakeRef> requires;
    std::shared_ptr<FlakeRegistry> lockFile;
    Value * vProvides; // FIXME: gc
    // commit hash
    // date
    // content hash
    Flake(FlakeRef & flakeRef) : ref(flakeRef) {};
};

Flake getFlake(EvalState &, const FlakeRef &);

FlakeRegistry updateLockFile(EvalState &, Flake &);

void updateLockFile(EvalState &, std::string);
}
