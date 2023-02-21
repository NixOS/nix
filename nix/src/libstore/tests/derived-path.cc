#include <regex>

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "tests/derived-path.hh"
#include "tests/libstore.hh"

namespace rc {
using namespace nix;

Gen<DerivedPath::Opaque> Arbitrary<DerivedPath::Opaque>::arbitrary()
{
    return gen::just(DerivedPath::Opaque {
        .path = *gen::arbitrary<StorePath>(),
    });
}

Gen<DerivedPath::Built> Arbitrary<DerivedPath::Built>::arbitrary()
{
    return gen::just(DerivedPath::Built {
        .drvPath = *gen::arbitrary<StorePath>(),
        .outputs = *gen::arbitrary<OutputsSpec>(),
    });
}

Gen<DerivedPath> Arbitrary<DerivedPath>::arbitrary()
{
    switch (*gen::inRange<uint8_t>(0, 1)) {
    case 0:
        return gen::just<DerivedPath>(*gen::arbitrary<DerivedPath::Opaque>());
    default:
        return gen::just<DerivedPath>(*gen::arbitrary<DerivedPath::Built>());
    }
}

}

namespace nix {

class DerivedPathTest : public LibStoreTest
{
};

// FIXME: `RC_GTEST_FIXTURE_PROP` isn't calling `SetUpTestSuite` because it is
// no a real fixture.
//
// See https://github.com/emil-e/rapidcheck/blob/master/doc/gtest.md#rc_gtest_fixture_propfixture-name-args
TEST_F(DerivedPathTest, force_init)
{
}

RC_GTEST_FIXTURE_PROP(
    DerivedPathTest,
    prop_round_rip,
    (const DerivedPath & o))
{
    RC_ASSERT(o == DerivedPath::parse(*store, o.to_string(*store)));
}

}
