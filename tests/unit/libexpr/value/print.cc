#include "tests/libexpr.hh"

#include "value.hh"
#include "print.hh"

namespace nix {

using namespace testing;

struct ValuePrintingTests : LibExprTest
{
    template<class... A>
    void test(Value v, std::string_view expected, A... args)
    {
        std::stringstream out;
        v.print(state, out, args...);
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

    auto list = state.buildList(3);
    list.elems[0] = &vOne;
    list.elems[1] = &vTwo;
    Value vList;
    vList.mkList(list);

    test(vList, "[ 1 2 «nullptr» ]");
}

TEST_F(ValuePrintingTests, vThunk)
{
    Value vThunk;
    vThunk.mkThunk(nullptr, nullptr);

    test(vThunk, "«thunk»");
}

TEST_F(ValuePrintingTests, vApp)
{
    Value vApp;
    vApp.mkApp(nullptr, nullptr);

    test(vApp, "«thunk»");
}

TEST_F(ValuePrintingTests, vLambda)
{
    Env env {
        .up = nullptr,
        .values = { }
    };
    PosTable::Origin origin = state.positions.addOrigin(std::monostate(), 1);
    auto posIdx = state.positions.add(origin, 0);
    auto body = ExprInt(0);
    auto formals = Formals {};

    ExprLambda eLambda(posIdx, createSymbol("a"), &formals, &body);

    Value vLambda;
    vLambda.mkLambda(&env, &eLambda);

    test(vLambda, "«lambda @ «none»:1:1»");

    eLambda.setName(createSymbol("puppy"));

    test(vLambda, "«lambda puppy @ «none»:1:1»");
}

TEST_F(ValuePrintingTests, vPrimOp)
{
    Value vPrimOp;
    PrimOp primOp{
        .name = "puppy"
    };
    vPrimOp.mkPrimOp(&primOp);

    test(vPrimOp, "«primop puppy»");
}

TEST_F(ValuePrintingTests, vPrimOpApp)
{
    PrimOp primOp{
        .name = "puppy"
    };
    Value vPrimOp;
    vPrimOp.mkPrimOp(&primOp);

    Value vPrimOpApp;
    vPrimOpApp.mkPrimOpApp(&vPrimOp, nullptr);

    test(vPrimOpApp, "«partially applied primop puppy»");
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

TEST_F(ValuePrintingTests, depthAttrs)
{
    Value vOne;
    vOne.mkInt(1);

    Value vTwo;
    vTwo.mkInt(2);

    BindingsBuilder builderEmpty(state, state.allocBindings(0));
    Value vAttrsEmpty;
    vAttrsEmpty.mkAttrs(builderEmpty.finish());

    BindingsBuilder builder(state, state.allocBindings(10));
    builder.insert(state.symbols.create("one"), &vOne);
    builder.insert(state.symbols.create("two"), &vTwo);
    builder.insert(state.symbols.create("nested"), &vAttrsEmpty);

    Value vAttrs;
    vAttrs.mkAttrs(builder.finish());

    BindingsBuilder builder2(state, state.allocBindings(10));
    builder2.insert(state.symbols.create("one"), &vOne);
    builder2.insert(state.symbols.create("two"), &vTwo);
    builder2.insert(state.symbols.create("nested"), &vAttrs);

    Value vNested;
    vNested.mkAttrs(builder2.finish());

    test(vNested, "{ nested = { ... }; one = 1; two = 2; }", PrintOptions { .maxDepth = 1 });
    test(vNested, "{ nested = { nested = { ... }; one = 1; two = 2; }; one = 1; two = 2; }", PrintOptions { .maxDepth = 2 });
    test(vNested, "{ nested = { nested = { }; one = 1; two = 2; }; one = 1; two = 2; }", PrintOptions { .maxDepth = 3 });
    test(vNested, "{ nested = { nested = { }; one = 1; two = 2; }; one = 1; two = 2; }", PrintOptions { .maxDepth = 4 });
}

TEST_F(ValuePrintingTests, depthList)
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

    BindingsBuilder builder2(state, state.allocBindings(10));
    builder2.insert(state.symbols.create("one"), &vOne);
    builder2.insert(state.symbols.create("two"), &vTwo);
    builder2.insert(state.symbols.create("nested"), &vAttrs);

    Value vNested;
    vNested.mkAttrs(builder2.finish());

    auto list = state.buildList(3);
    list.elems[0] = &vOne;
    list.elems[1] = &vTwo;
    list.elems[2] = &vNested;
    Value vList;
    vList.mkList(list);

    test(vList, "[ 1 2 { ... } ]", PrintOptions { .maxDepth = 1 });
    test(vList, "[ 1 2 { nested = { ... }; one = 1; two = 2; } ]", PrintOptions { .maxDepth = 2 });
    test(vList, "[ 1 2 { nested = { one = 1; two = 2; }; one = 1; two = 2; } ]", PrintOptions { .maxDepth = 3 });
    test(vList, "[ 1 2 { nested = { one = 1; two = 2; }; one = 1; two = 2; } ]", PrintOptions { .maxDepth = 4 });
    test(vList, "[ 1 2 { nested = { one = 1; two = 2; }; one = 1; two = 2; } ]", PrintOptions { .maxDepth = 5 });
}

struct StringPrintingTests : LibExprTest
{
    template<class... A>
    void test(std::string_view literal, std::string_view expected, unsigned int maxLength, A... args)
    {
        Value v;
        v.mkString(literal);

        std::stringstream out;
        printValue(state, out, v, PrintOptions {
            .maxStringLength = maxLength
        });
        ASSERT_EQ(out.str(), expected);
    }
};

TEST_F(StringPrintingTests, maxLengthTruncation)
{
    test("abcdefghi", "\"abcdefghi\"", 10);
    test("abcdefghij", "\"abcdefghij\"", 10);
    test("abcdefghijk", "\"abcdefghij\" «1 byte elided»", 10);
    test("abcdefghijkl", "\"abcdefghij\" «2 bytes elided»", 10);
    test("abcdefghijklm", "\"abcdefghij\" «3 bytes elided»", 10);
}

// Check that printing an attrset shows 'important' attributes like `type`
// first, but only reorder the attrs when we have a maxAttrs budget.
TEST_F(ValuePrintingTests, attrsTypeFirst)
{
    Value vType;
    vType.mkString("puppy");

    Value vApple;
    vApple.mkString("apple");

    BindingsBuilder builder(state, state.allocBindings(10));
    builder.insert(state.symbols.create("type"), &vType);
    builder.insert(state.symbols.create("apple"), &vApple);

    Value vAttrs;
    vAttrs.mkAttrs(builder.finish());

    test(vAttrs,
         "{ type = \"puppy\"; apple = \"apple\"; }",
         PrintOptions {
            .maxAttrs = 100
         });

    test(vAttrs,
         "{ apple = \"apple\"; type = \"puppy\"; }",
         PrintOptions { });
}

TEST_F(ValuePrintingTests, ansiColorsInt)
{
    Value v;
    v.mkInt(10);

    test(v,
         ANSI_CYAN "10" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsFloat)
{
    Value v;
    v.mkFloat(1.6);

    test(v,
         ANSI_CYAN "1.6" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsBool)
{
    Value v;
    v.mkBool(true);

    test(v,
         ANSI_CYAN "true" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsString)
{
    Value v;
    v.mkString("puppy");

    test(v,
         ANSI_MAGENTA "\"puppy\"" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
        });
}

TEST_F(ValuePrintingTests, ansiColorsStringElided)
{
    Value v;
    v.mkString("puppy");

    test(v,
         ANSI_MAGENTA "\"pup\" " ANSI_FAINT "«2 bytes elided»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true,
             .maxStringLength = 3
         });
}

TEST_F(ValuePrintingTests, ansiColorsPath)
{
    Value v;
    v.mkPath(state.rootPath(CanonPath("puppy")));

    test(v,
         ANSI_GREEN "/puppy" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsNull)
{
    Value v;
    v.mkNull();

    test(v,
         ANSI_CYAN "null" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsAttrs)
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

    test(vAttrs,
         "{ one = " ANSI_CYAN "1" ANSI_NORMAL "; two = " ANSI_CYAN "2" ANSI_NORMAL "; }",
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsDerivation)
{
    Value vDerivation;
    vDerivation.mkString("derivation");

    BindingsBuilder builder(state, state.allocBindings(10));
    builder.insert(state.sType, &vDerivation);

    Value vAttrs;
    vAttrs.mkAttrs(builder.finish());

    test(vAttrs,
         ANSI_GREEN "«derivation»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true,
             .force = true,
             .derivationPaths = true
         });

    test(vAttrs,
         "{ type = " ANSI_MAGENTA "\"derivation\"" ANSI_NORMAL "; }",
         PrintOptions {
             .ansiColors = true,
             .force = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsError)
{
    Value throw_ = state.getBuiltin("throw");
    Value message;
    message.mkString("uh oh!");
    Value vError;
    vError.mkApp(&throw_, &message);

    test(vError,
         ANSI_RED
         "«error: uh oh!»"
         ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true,
             .force = true,
         });
}

TEST_F(ValuePrintingTests, ansiColorsDerivationError)
{
    Value throw_ = state.getBuiltin("throw");
    Value message;
    message.mkString("uh oh!");
    Value vError;
    vError.mkApp(&throw_, &message);

    Value vDerivation;
    vDerivation.mkString("derivation");

    BindingsBuilder builder(state, state.allocBindings(10));
    builder.insert(state.sType, &vDerivation);
    builder.insert(state.sDrvPath, &vError);

    Value vAttrs;
    vAttrs.mkAttrs(builder.finish());

    test(vAttrs,
         "{ drvPath = "
         ANSI_RED
         "«error: uh oh!»"
         ANSI_NORMAL
         "; type = "
         ANSI_MAGENTA
         "\"derivation\""
         ANSI_NORMAL
         "; }",
         PrintOptions {
             .ansiColors = true,
             .force = true
         });

    test(vAttrs,
         ANSI_RED
         "«error: uh oh!»"
         ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true,
             .force = true,
             .derivationPaths = true,
         });
}

TEST_F(ValuePrintingTests, ansiColorsAssert)
{
    ExprVar eFalse(state.symbols.create("false"));
    eFalse.bindVars(state, state.staticBaseEnv);
    ExprInt eInt(1);

    ExprAssert expr(noPos, &eFalse, &eInt);

    Value v;
    state.mkThunk_(v, &expr);

    test(v,
         ANSI_RED "«error: assertion 'false' failed»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true,
             .force = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsList)
{
    Value vOne;
    vOne.mkInt(1);

    Value vTwo;
    vTwo.mkInt(2);

    auto list = state.buildList(3);
    list.elems[0] = &vOne;
    list.elems[1] = &vTwo;
    Value vList;
    vList.mkList(list);

    test(vList,
         "[ " ANSI_CYAN "1" ANSI_NORMAL " " ANSI_CYAN "2" ANSI_NORMAL " " ANSI_MAGENTA "«nullptr»" ANSI_NORMAL " ]",
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsLambda)
{
    Env env {
        .up = nullptr,
        .values = { }
    };
    PosTable::Origin origin = state.positions.addOrigin(std::monostate(), 1);
    auto posIdx = state.positions.add(origin, 0);
    auto body = ExprInt(0);
    auto formals = Formals {};

    ExprLambda eLambda(posIdx, createSymbol("a"), &formals, &body);

    Value vLambda;
    vLambda.mkLambda(&env, &eLambda);

    test(vLambda,
         ANSI_BLUE "«lambda @ «none»:1:1»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true,
             .force = true
         });

    eLambda.setName(createSymbol("puppy"));

    test(vLambda,
         ANSI_BLUE "«lambda puppy @ «none»:1:1»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true,
             .force = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsPrimOp)
{
    PrimOp primOp{
        .name = "puppy"
    };
    Value v;
    v.mkPrimOp(&primOp);

    test(v,
         ANSI_BLUE "«primop puppy»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsPrimOpApp)
{
    PrimOp primOp{
        .name = "puppy"
    };
    Value vPrimOp;
    vPrimOp.mkPrimOp(&primOp);

    Value v;
    v.mkPrimOpApp(&vPrimOp, nullptr);

    test(v,
         ANSI_BLUE "«partially applied primop puppy»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsThunk)
{
    Value v;
    v.mkThunk(nullptr, nullptr);

    test(v,
         ANSI_MAGENTA "«thunk»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsBlackhole)
{
    Value v;
    v.mkBlackhole();

    test(v,
         ANSI_RED "«potential infinite recursion»" ANSI_NORMAL,
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsAttrsRepeated)
{
    BindingsBuilder emptyBuilder(state, state.allocBindings(1));

    Value vEmpty;
    vEmpty.mkAttrs(emptyBuilder.finish());

    BindingsBuilder builder(state, state.allocBindings(10));
    builder.insert(state.symbols.create("a"), &vEmpty);
    builder.insert(state.symbols.create("b"), &vEmpty);

    Value vAttrs;
    vAttrs.mkAttrs(builder.finish());

    test(vAttrs,
         "{ a = { }; b = " ANSI_MAGENTA "«repeated»" ANSI_NORMAL "; }",
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, ansiColorsListRepeated)
{
    BindingsBuilder emptyBuilder(state, state.allocBindings(1));

    Value vEmpty;
    vEmpty.mkAttrs(emptyBuilder.finish());

    auto list = state.buildList(2);
    list.elems[0] = &vEmpty;
    list.elems[1] = &vEmpty;
    Value vList;
    vList.mkList(list);

    test(vList,
         "[ { } " ANSI_MAGENTA "«repeated»" ANSI_NORMAL " ]",
         PrintOptions {
             .ansiColors = true
         });
}

TEST_F(ValuePrintingTests, listRepeated)
{
    BindingsBuilder emptyBuilder(state, state.allocBindings(1));

    Value vEmpty;
    vEmpty.mkAttrs(emptyBuilder.finish());

    auto list = state.buildList(2);
    list.elems[0] = &vEmpty;
    list.elems[1] = &vEmpty;
    Value vList;
    vList.mkList(list);

    test(vList, "[ { } «repeated» ]", PrintOptions { });
    test(vList,
         "[ { } { } ]",
         PrintOptions {
             .trackRepeated = false
         });
}

TEST_F(ValuePrintingTests, ansiColorsAttrsElided)
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

    test(vAttrs,
         "{ one = " ANSI_CYAN "1" ANSI_NORMAL "; " ANSI_FAINT "«1 attribute elided»" ANSI_NORMAL " }",
         PrintOptions {
             .ansiColors = true,
             .maxAttrs = 1
         });

    Value vThree;
    vThree.mkInt(3);

    builder.insert(state.symbols.create("three"), &vThree);
    vAttrs.mkAttrs(builder.finish());

    test(vAttrs,
         "{ one = " ANSI_CYAN "1" ANSI_NORMAL "; " ANSI_FAINT "«2 attributes elided»" ANSI_NORMAL " }",
         PrintOptions {
             .ansiColors = true,
             .maxAttrs = 1
         });
}

TEST_F(ValuePrintingTests, ansiColorsListElided)
{
    BindingsBuilder emptyBuilder(state, state.allocBindings(1));

    Value vOne;
    vOne.mkInt(1);

    Value vTwo;
    vTwo.mkInt(2);

    {
    auto list = state.buildList(2);
    list.elems[0] = &vOne;
    list.elems[1] = &vTwo;
    Value vList;
    vList.mkList(list);

    test(vList,
         "[ " ANSI_CYAN "1" ANSI_NORMAL " " ANSI_FAINT "«1 item elided»" ANSI_NORMAL " ]",
         PrintOptions {
             .ansiColors = true,
             .maxListItems = 1
         });
    }

    Value vThree;
    vThree.mkInt(3);

    {
    auto list = state.buildList(3);
    list.elems[0] = &vOne;
    list.elems[1] = &vTwo;
    list.elems[2] = &vThree;
    Value vList;
    vList.mkList(list);

    test(vList,
         "[ " ANSI_CYAN "1" ANSI_NORMAL " " ANSI_FAINT "«2 items elided»" ANSI_NORMAL " ]",
         PrintOptions {
             .ansiColors = true,
             .maxListItems = 1
         });
    }
}

} // namespace nix
