#include "nix/expr/tests/libexpr.hh"
#include "nix/expr/value-to-json.hh"
#include "nix/expr/static-string-data.hh"
#include "nix/util/tests/gmock-matchers.hh"

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
    v.mkPath(state.rootPath(CanonPath("/test")), state.mem);
    ASSERT_EQ(getJSONValue(v), "\"/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-x\"");
}

TEST_F(JSONValueTest, SelfReferentialAttrs)
{
    // let a = { b = a; }; in builtins.toJSON a
    Value vAttrs;
    BindingsBuilder builder = state.buildBindings(1);
    builder.insert(state.symbols.create("b"), &vAttrs);
    vAttrs.mkAttrs(builder.finish());

    ASSERT_THROW(getJSONValue(vAttrs), InfiniteRecursionError);
}

TEST_F(JSONValueTest, SelfReferentialList)
{
    // let xs = [ xs ]; in builtins.toJSON xs
    Value vList;
    auto list = state.buildList(1);
    list.elems[0] = &vList;
    vList.mkList(list);

    ASSERT_THROW(getJSONValue(vList), InfiniteRecursionError);
}

TEST_F(JSONValueTest, SharedSubtreeIsNotACycle)
{
    // { p = x; q = x; } where x = { v = 1; }
    BindingsBuilder innerBuilder = state.buildBindings(1);
    Value vOne;
    vOne.mkInt(1);
    innerBuilder.insert(state.symbols.create("v"), &vOne);

    Value vShared;
    vShared.mkAttrs(innerBuilder.finish());

    BindingsBuilder outerBuilder = state.buildBindings(2);
    outerBuilder.insert(state.symbols.create("p"), &vShared);
    outerBuilder.insert(state.symbols.create("q"), &vShared);

    Value vOuter;
    vOuter.mkAttrs(outerBuilder.finish());

    ASSERT_EQ(getJSONValue(vOuter), "{\"p\":{\"v\":1},\"q\":{\"v\":1}}");
}

TEST_F(JSONValueTest, CycleErrorNamesBothEnds)
{
    // { p = inner; q = inner; } where inner.self = inner.
    // The slow retry should report both ends of the cycle, naming the
    // cycle target by path rather than only listing the path leading to it.
    Value vInner;
    BindingsBuilder innerBuilder = state.buildBindings(1);
    innerBuilder.insert(state.symbols.create("self"), &vInner);
    vInner.mkAttrs(innerBuilder.finish());

    BindingsBuilder outerBuilder = state.buildBindings(2);
    outerBuilder.insert(state.symbols.create("p"), &vInner);
    outerBuilder.insert(state.symbols.create("q"), &vInner);

    Value vOuter;
    vOuter.mkAttrs(outerBuilder.finish());

    ASSERT_THAT(
        [&]() { getJSONValue(vOuter); },
        ::testing::ThrowsMessage<InfiniteRecursionError>(
            ::nix::testing::HasSubstrIgnoreANSIMatcher("is the same as the value at")));
}
} /* namespace nix */
