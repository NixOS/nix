#include "value/context.hh"

#include "tests/libexpr.hh"

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
