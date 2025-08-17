#include <regex>

#include <exception> // Needed by rapidcheck on Darwin
#include <rapidcheck.h>

#include "nix/store/tests/derived-path.hh"

namespace rc {
using namespace nix;

Gen<SingleDerivedPath::Opaque> Arbitrary<SingleDerivedPath::Opaque>::arbitrary()
{
    return gen::map(gen::arbitrary<StorePath>(), [](StorePath path) {
        return DerivedPath::Opaque{
            .path = path,
        };
    });
}

Gen<SingleDerivedPath::Built> Arbitrary<SingleDerivedPath::Built>::arbitrary()
{
    return gen::mapcat(gen::arbitrary<SingleDerivedPath>(), [](SingleDerivedPath drvPath) {
        return gen::map(gen::arbitrary<StorePathName>(), [drvPath](StorePathName outputPath) {
            return SingleDerivedPath::Built{
                .drvPath = make_ref<SingleDerivedPath>(drvPath),
                .output = outputPath.name,
            };
        });
    });
}

Gen<DerivedPath::Built> Arbitrary<DerivedPath::Built>::arbitrary()
{
    return gen::mapcat(gen::arbitrary<SingleDerivedPath>(), [](SingleDerivedPath drvPath) {
        return gen::map(gen::arbitrary<OutputsSpec>(), [drvPath](OutputsSpec outputs) {
            return DerivedPath::Built{
                .drvPath = make_ref<SingleDerivedPath>(drvPath),
                .outputs = outputs,
            };
        });
    });
}

Gen<SingleDerivedPath> Arbitrary<SingleDerivedPath>::arbitrary()
{
    return gen::mapcat(gen::inRange<uint8_t>(0, std::variant_size_v<SingleDerivedPath::Raw>), [](uint8_t n) {
        switch (n) {
        case 0:
            return gen::map(gen::arbitrary<SingleDerivedPath::Opaque>(), [](SingleDerivedPath a) { return a; });
        case 1:
            return gen::map(gen::arbitrary<SingleDerivedPath::Built>(), [](SingleDerivedPath a) { return a; });
        default:
            assert(false);
        }
    });
}

Gen<DerivedPath> Arbitrary<DerivedPath>::arbitrary()
{
    return gen::mapcat(gen::inRange<uint8_t>(0, std::variant_size_v<DerivedPath::Raw>), [](uint8_t n) {
        switch (n) {
        case 0:
            return gen::map(gen::arbitrary<DerivedPath::Opaque>(), [](DerivedPath a) { return a; });
        case 1:
            return gen::map(gen::arbitrary<DerivedPath::Built>(), [](DerivedPath a) { return a; });
        default:
            assert(false);
        }
    });
}

} // namespace rc
