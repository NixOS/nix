#include <exception> // IWYU pragma: keep (Needed by rapidcheck on Darwin and FreeBSD)
#include <rapidcheck.h>

#include "nix/store/tests/derived-path.hh"

namespace rc {

Gen<nix::SingleDerivedPath::Opaque> Arbitrary<nix::SingleDerivedPath::Opaque>::arbitrary()
{
    using namespace nix;
    return gen::map(gen::arbitrary<StorePath>(), [](StorePath path) {
        return DerivedPath::Opaque{
            .path = path,
        };
    });
}

Gen<nix::SingleDerivedPath::Built> Arbitrary<nix::SingleDerivedPath::Built>::arbitrary()
{
    using namespace nix;
    return gen::mapcat(gen::arbitrary<SingleDerivedPath>(), [](SingleDerivedPath drvPath) {
        return gen::map(gen::arbitrary<StorePathName>(), [drvPath](StorePathName outputPath) {
            return SingleDerivedPath::Built{
                .drvPath = make_ref<SingleDerivedPath>(drvPath),
                .output = outputPath.name,
            };
        });
    });
}

Gen<nix::DerivedPath::Built> Arbitrary<nix::DerivedPath::Built>::arbitrary()
{
    using namespace nix;
    return gen::mapcat(gen::arbitrary<SingleDerivedPath>(), [](SingleDerivedPath drvPath) {
        return gen::map(gen::arbitrary<OutputsSpec>(), [drvPath](OutputsSpec outputs) {
            return DerivedPath::Built{
                .drvPath = make_ref<SingleDerivedPath>(drvPath),
                .outputs = outputs,
            };
        });
    });
}

Gen<nix::SingleDerivedPath> Arbitrary<nix::SingleDerivedPath>::arbitrary()
{
    using namespace nix;
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

Gen<nix::DerivedPath> Arbitrary<nix::DerivedPath>::arbitrary()
{
    using namespace nix;
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
