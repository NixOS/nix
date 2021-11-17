#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "tests/path.hh"
#include "tests/libexpr.hh"
#include "tests/value/context.hh"

namespace nix {

// Testing of trivial expressions
struct NixStringContextElemTest : public LibExprTest {
   const Store & store() const {
       return *LibExprTest::store;
   }
};

TEST_F(NixStringContextElemTest, empty_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse(store(), ""),
        BadNixStringContextElem);
}

TEST_F(NixStringContextElemTest, single_bang_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse(store(), "!"),
        BadNixStringContextElem);
}

TEST_F(NixStringContextElemTest, double_bang_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse(store(), "!!/"),
        BadStorePath);
}

TEST_F(NixStringContextElemTest, eq_slash_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse(store(), "=/"),
        BadStorePath);
}

TEST_F(NixStringContextElemTest, slash_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse(store(), "/"),
        BadStorePath);
}

TEST_F(NixStringContextElemTest, opaque) {
    std::string_view opaque = "/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x";
    auto elem = NixStringContextElem::parse(store(), opaque);
    auto * p = std::get_if<NixStringContextElem::Opaque>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->path, store().parseStorePath(opaque));
    ASSERT_EQ(elem.to_string(store()), opaque);
}

TEST_F(NixStringContextElemTest, drvDeep) {
    std::string_view drvDeep = "=/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(store(), drvDeep);
    auto * p = std::get_if<NixStringContextElem::DrvDeep>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->drvPath, store().parseStorePath(drvDeep.substr(1)));
    ASSERT_EQ(elem.to_string(store()), drvDeep);
}

TEST_F(NixStringContextElemTest, built) {
    std::string_view built = "!foo!/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(store(), built);
    auto * p = std::get_if<NixStringContextElem::Built>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->output, "foo");
    ASSERT_EQ(p->drvPath, store().parseStorePath(built.substr(5)));
    ASSERT_EQ(elem.to_string(store()), built);
}

}

namespace rc {
using namespace nix;

Gen<NixStringContextElem::Opaque> Arbitrary<NixStringContextElem::Opaque>::arbitrary()
{
    return gen::just(NixStringContextElem::Opaque {
        .path = *gen::arbitrary<StorePath>(),
    });
}

Gen<NixStringContextElem::DrvDeep> Arbitrary<NixStringContextElem::DrvDeep>::arbitrary()
{
    return gen::just(NixStringContextElem::DrvDeep {
        .drvPath = *gen::arbitrary<StorePath>(),
    });
}

Gen<NixStringContextElem::Built> Arbitrary<NixStringContextElem::Built>::arbitrary()
{
    return gen::just(NixStringContextElem::Built {
        .drvPath = *gen::arbitrary<StorePath>(),
        .output = (*gen::arbitrary<StorePathName>()).name,
    });
}

Gen<NixStringContextElem> Arbitrary<NixStringContextElem>::arbitrary()
{
    switch (*gen::inRange<uint8_t>(0, 2)) {
    case 0:
        return gen::just<NixStringContextElem>(*gen::arbitrary<NixStringContextElem::Opaque>());
    case 1:
        return gen::just<NixStringContextElem>(*gen::arbitrary<NixStringContextElem::DrvDeep>());
    default:
        return gen::just<NixStringContextElem>(*gen::arbitrary<NixStringContextElem::Built>());
    }
}

}

namespace nix {

RC_GTEST_FIXTURE_PROP(
    NixStringContextElemTest,
    prop_round_rip,
    (const NixStringContextElem & o))
{
    RC_ASSERT(o == NixStringContextElem::parse(store(), o.to_string(store())));
}

}
