#include "tests/outputs-spec.hh"

#include <rapidcheck.h>

namespace rc {
using namespace nix;

Gen<OutputsSpec> Arbitrary<OutputsSpec>::arbitrary()
{
    switch (*gen::inRange<uint8_t>(0, std::variant_size_v<OutputsSpec::Raw>)) {
    case 0:
        return gen::just((OutputsSpec) OutputsSpec::All { });
    case 1:
        return gen::just((OutputsSpec) OutputsSpec::Names {
            *gen::nonEmpty(gen::container<StringSet>(gen::map(
                gen::arbitrary<StorePathName>(),
                [](StorePathName n) { return n.name; }))),
        });
    default:
        assert(false);
    }
}

}
