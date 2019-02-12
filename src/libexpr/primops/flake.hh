#include "types.hh"
#include "flakeref.hh"

#include <variant>

namespace nix {

struct FlakeRegistry
{
    struct Entry
    {
        FlakeRef ref;
    };
    std::map<FlakeId, Entry> entries;
};

}
