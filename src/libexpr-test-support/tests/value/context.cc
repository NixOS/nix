#include <exception> // Needed by rapidcheck on Darwin
#include <rapidcheck.h>

#include "nix/store/tests/path.hh"
#include "nix/expr/tests/value/context.hh"

namespace rc {
using namespace nix;

Gen<NixStringContextElem::DrvDeep> Arbitrary<NixStringContextElem::DrvDeep>::arbitrary()
{
    return gen::map(gen::arbitrary<StorePath>(), [](StorePath drvPath) {
        return NixStringContextElem::DrvDeep{
            .drvPath = drvPath,
        };
    });
}

Gen<NixStringContextElem> Arbitrary<NixStringContextElem>::arbitrary()
{
    return gen::mapcat(
        gen::inRange<uint8_t>(0, std::variant_size_v<NixStringContextElem::Raw>),
        [](uint8_t n) -> Gen<NixStringContextElem> {
            switch (n) {
            case 0:
                return gen::map(
                    gen::arbitrary<NixStringContextElem::Opaque>(), [](NixStringContextElem a) { return a; });
            case 1:
                return gen::map(
                    gen::arbitrary<NixStringContextElem::DrvDeep>(), [](NixStringContextElem a) { return a; });
            case 2:
                return gen::map(
                    gen::arbitrary<NixStringContextElem::Built>(), [](NixStringContextElem a) { return a; });
            default:
                assert(false);
            }
        });
}

} // namespace rc
