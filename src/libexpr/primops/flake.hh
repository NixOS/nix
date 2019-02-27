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
    };
    std::map<FlakeId, Entry> entries;
};

Value * makeFlakeRegistryValue(EvalState & state);

Value * makeFlakeValue(EvalState & state, std::string flakeUri, Value & v);

struct Flake
{
    FlakeId id;
    std::string description;
    Path path;
    std::vector<FlakeRef> requires;
    std::unique_ptr<FlakeRegistry> lockFile;
    Value * vProvides; // FIXME: gc
    // commit hash
    // date
    // content hash
};

Flake getFlake(EvalState & state, const FlakeRef & flakeRef);
}
