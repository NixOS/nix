#include "nix/expr/value.hh"

#include "nix/store/tests/libstore.hh"

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

} // namespace nix
