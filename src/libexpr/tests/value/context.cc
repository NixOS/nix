#include <nlohmann/json.hpp>
#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>

#include "tests/path.hh"
#include "tests/libexpr.hh"
#include "tests/value/context.hh"

namespace nix {

TEST(NixStringContextElemTest, empty_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse(""),
        BadNixStringContextElem);
}

TEST(NixStringContextElemTest, single_bang_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("!"),
        BadNixStringContextElem);
}

TEST(NixStringContextElemTest, double_bang_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("!!/"),
        BadStorePath);
}

TEST(NixStringContextElemTest, eq_slash_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("=/"),
        BadStorePath);
}

TEST(NixStringContextElemTest, slash_invalid) {
    EXPECT_THROW(
        NixStringContextElem::parse("/"),
        BadStorePath);
}

TEST(NixStringContextElemTest, opaque) {
    std::string_view opaque = "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x";
    auto elem = NixStringContextElem::parse(opaque);
    auto * p = std::get_if<NixStringContextElem::Opaque>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->path, StorePath { opaque });
    ASSERT_EQ(elem.to_string(), opaque);
}

TEST(NixStringContextElemTest, drvDeep) {
    std::string_view drvDeep = "=g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(drvDeep);
    auto * p = std::get_if<NixStringContextElem::DrvDeep>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->drvPath, StorePath { drvDeep.substr(1) });
    ASSERT_EQ(elem.to_string(), drvDeep);
}

TEST(NixStringContextElemTest, built) {
    std::string_view built = "!foo!g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x.drv";
    auto elem = NixStringContextElem::parse(built);
    auto * p = std::get_if<NixStringContextElem::Built>(&elem);
    ASSERT_TRUE(p);
    ASSERT_EQ(p->output, "foo");
    ASSERT_EQ(p->drvPath, StorePath { built.substr(5) });
    ASSERT_EQ(elem.to_string(), built);
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

namespace nix {

#if 0
RC_GTEST_PROP(
    NixStringContextElemTest,
    prop_round_rip,
    (const NixStringContextElem & o))
{
    RC_ASSERT(o == NixStringContextElem::parse(o.to_string()));
}
#endif

}
