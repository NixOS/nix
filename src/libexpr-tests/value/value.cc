#include "nix/expr/value.hh"
#include "nix/expr/static-string-data.hh"

#include "nix/store/tests/libstore.hh"
#include <gtest/gtest.h>

namespace nix {

class ValueTest : public LibStoreTest
{};

TEST_F(ValueTest, unsetValue)
{
    Value unsetValue;
    ASSERT_EQ(false, unsetValue.isValid());
    ASSERT_EQ(nThunk, unsetValue.type(true));
    ASSERT_DEATH(unsetValue.type(), "");
}

TEST_F(ValueTest, vInt)
{
    Value vInt;
    vInt.mkInt(42);
    ASSERT_EQ(true, vInt.isValid());
}

TEST_F(ValueTest, staticString)
{
    Value vStr1;
    Value vStr2;
    vStr1.mkStringNoCopy("foo"_sds);
    vStr2.mkStringNoCopy("foo"_sds);

    auto & sd1 = vStr1.string_data();
    auto & sd2 = vStr2.string_data();

    // The strings should be the same
    ASSERT_EQ(sd1.view(), sd2.view());

    // The strings should also be backed by the same (static) allocation
    ASSERT_EQ(&sd1, &sd2);
}

} // namespace nix
