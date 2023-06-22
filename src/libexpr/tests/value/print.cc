#include "tests/libexpr.hh"

#include "value.hh"

namespace nix {

using namespace testing;

struct ValuePrintingTests : LibExprTest
{
    template<class... A>
    void test(Value v, std::string_view expected, A... args)
    {
        std::stringstream out;
        v.print(state.symbols, out, args...);
        ASSERT_EQ(out.str(), expected);
    }
};

TEST_F(ValuePrintingTests, tInt)
{
    Value vInt;
    vInt.mkInt(10);
    test(vInt, "10");
}

TEST_F(ValuePrintingTests, tBool)
{
    Value vBool;
    vBool.mkBool(true);
    test(vBool, "true");
}

TEST_F(ValuePrintingTests, tString)
{
    Value vString;
    vString.mkString("some-string");
    test(vString, "\"some-string\"");
}

TEST_F(ValuePrintingTests, tPath)
{
    Value vPath;
    vPath.mkString("/foo");
    test(vPath, "\"/foo\"");
}

TEST_F(ValuePrintingTests, tNull)
{
    Value vNull;
    vNull.mkNull();
    test(vNull, "null");
}

TEST_F(ValuePrintingTests, tAttrs)
{
    Value vOne;
    vOne.mkInt(1);

    Value vTwo;
    vTwo.mkInt(2);

    BindingsBuilder builder(state, state.allocBindings(10));
    builder.insert(state.symbols.create("one"), &vOne);
    builder.insert(state.symbols.create("two"), &vTwo);

    Value vAttrs;
    vAttrs.mkAttrs(builder.finish());

    test(vAttrs, "{ one = 1; two = 2; }");
}

TEST_F(ValuePrintingTests, tList)
{
    Value vOne;
    vOne.mkInt(1);

    Value vTwo;
    vTwo.mkInt(2);

    Value vList;
    state.mkList(vList, 5);
    vList.bigList.elems[0] = &vOne;
    vList.bigList.elems[1] = &vTwo;
    vList.bigList.size = 3;

    test(vList, "[ 1 2 (nullptr) ]");
}

TEST_F(ValuePrintingTests, vThunk)
{
    Value vThunk;
    vThunk.mkThunk(nullptr, nullptr);

    test(vThunk, "<CODE>");
}

TEST_F(ValuePrintingTests, vApp)
{
    Value vApp;
    vApp.mkApp(nullptr, nullptr);

    test(vApp, "<CODE>");
}

TEST_F(ValuePrintingTests, vLambda)
{
    Value vLambda;
    vLambda.mkLambda(nullptr, nullptr);

    test(vLambda, "<LAMBDA>");
}

TEST_F(ValuePrintingTests, vPrimOp)
{
    Value vPrimOp;
    vPrimOp.mkPrimOp(nullptr);

    test(vPrimOp, "<PRIMOP>");
}

TEST_F(ValuePrintingTests, vPrimOpApp)
{
    Value vPrimOpApp;
    vPrimOpApp.mkPrimOpApp(nullptr, nullptr);

    test(vPrimOpApp, "<PRIMOP-APP>");
}

TEST_F(ValuePrintingTests, vExternal)
{
    struct MyExternal : ExternalValueBase
    {
    public:
        std::string showType() const override
        {
            return "";
        }
        std::string typeOf() const override
        {
            return "";
        }
        virtual std::ostream & print(std::ostream & str) const override
        {
            str << "testing-external!";
            return str;
        }
    } myExternal;
    Value vExternal;
    vExternal.mkExternal(&myExternal);

    test(vExternal, "testing-external!");
}

TEST_F(ValuePrintingTests, vFloat)
{
    Value vFloat;
    vFloat.mkFloat(2.0);

    test(vFloat, "2");
}

TEST_F(ValuePrintingTests, vBlackhole)
{
    Value vBlackhole;
    vBlackhole.mkBlackhole();
    test(vBlackhole, "«potential infinite recursion»");
}

} // namespace nix
