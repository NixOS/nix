#include <rapidcheck.h>

#include "tests/path.hh"
#include "tests/value/context.hh"

namespace rc {
using namespace nix;

Gen<NixStringContextElem::DrvDeep> Arbitrary<NixStringContextElem::DrvDeep>::arbitrary()
{
    return gen::just(NixStringContextElem::DrvDeep {
        .drvPath = *gen::arbitrary<StorePath>(),
    });
}

Gen<NixStringContextElem> Arbitrary<NixStringContextElem>::arbitrary()
{
    switch (*gen::inRange<uint8_t>(0, std::variant_size_v<NixStringContextElem::Raw>)) {
    case 0:
        return gen::just<NixStringContextElem>(*gen::arbitrary<NixStringContextElem::Opaque>());
    case 1:
        return gen::just<NixStringContextElem>(*gen::arbitrary<NixStringContextElem::DrvDeep>());
    case 2:
        return gen::just<NixStringContextElem>(*gen::arbitrary<NixStringContextElem::Built>());
    default:
        assert(false);
    }
}

}
