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

}
