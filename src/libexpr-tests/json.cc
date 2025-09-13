#include "nix/expr/tests/libexpr.hh"
#include "nix/expr/value-to-json.hh"
#include "nix/expr/static-string-data.hh"

namespace nix {
// Testing the conversion to JSON

class JSONValueTest : public LibExprTest
{
protected:
    std::string getJSONValue(Value & value)
    {
        std::stringstream ss;
        NixStringContext ps;
        printValueAsJSON(state, true, value, noPos, ss, ps);
        return ss.str();
    }
};

TEST_F(JSONValueTest, null)
{
    Value v;
    v.mkNull();
    ASSERT_EQ(getJSONValue(v), "null");
}

TEST_F(JSONValueTest, BoolFalse)
{
    Value v;
    v.mkBool(false);
    ASSERT_EQ(getJSONValue(v), "false");
}

TEST_F(JSONValueTest, BoolTrue)
{
    Value v;
    v.mkBool(true);
    ASSERT_EQ(getJSONValue(v), "true");
}

TEST_F(JSONValueTest, IntPositive)
{
    Value v;
    v.mkInt(100);
    ASSERT_EQ(getJSONValue(v), "100");
}

TEST_F(JSONValueTest, IntNegative)
{
    Value v;
    v.mkInt(-100);
    ASSERT_EQ(getJSONValue(v), "-100");
}

TEST_F(JSONValueTest, String)
{
    Value v;
    v.mkStringNoCopy("test"_sds);
    ASSERT_EQ(getJSONValue(v), "\"test\"");
}

TEST_F(JSONValueTest, StringQuotes)
{
    Value v;

    v.mkStringNoCopy("test\""_sds);
    ASSERT_EQ(getJSONValue(v), "\"test\\\"\"");
}

// The dummy store doesn't support writing files. Fails with this exception message:
// C++ exception with description "error: operation 'addToStoreFromDump' is
// not supported by store 'dummy'" thrown in the test body.
TEST_F(JSONValueTest, DISABLED_Path)
{
    Value v;
    v.mkPath(state.rootPath(CanonPath("/test")));
    ASSERT_EQ(getJSONValue(v), "\"/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x\"");
}
} /* namespace nix */
