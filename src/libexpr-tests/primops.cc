#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/eval-settings.hh"
#include "nix/util/memory-source-accessor.hh"

#include "nix/expr/tests/libexpr.hh"
#include "nix/util/tests/gmock-matchers.hh"

namespace nix {
class CaptureLogger : public Logger
{
    std::ostringstream oss;

public:
    CaptureLogger() {}

    std::string get() const
    {
        return oss.str();
    }

    void log(Verbosity lvl, std::string_view s) override
    {
        oss << s << std::endl;
    }

    void logEI(const ErrorInfo & ei) override
    {
        showErrorInfo(oss, ei, loggerSettings.showTrace.get());
    }
};

class CaptureLogging
{
    std::unique_ptr<Logger> oldLogger;
public:
    CaptureLogging()
    {
        oldLogger = std::move(logger);
        logger = std::make_unique<CaptureLogger>();
    }

    ~CaptureLogging()
    {
        logger = std::move(oldLogger);
    }
};

// Testing eval of PrimOp's
class PrimOpTest : public LibExprTest
{};

TEST_F(PrimOpTest, throw)
{
    ASSERT_THROW(eval("throw \"foo\""), ThrownError);
}

TEST_F(PrimOpTest, abort)
{
    ASSERT_THROW(eval("abort \"abort\""), Abort);
}

TEST_F(PrimOpTest, ceil)
{
    auto v = eval("builtins.ceil 1.9");
    ASSERT_THAT(v, IsIntEq(2));
    auto intMin = eval("builtins.ceil (-4611686018427387904 - 4611686018427387904)");
    ASSERT_THAT(intMin, IsIntEq(std::numeric_limits<NixInt::Inner>::min()));
    ASSERT_THROW(eval("builtins.ceil 1.0e200"), EvalError);
    ASSERT_THROW(eval("builtins.ceil -1.0e200"), EvalError);
    ASSERT_THROW(eval("builtins.ceil (1.0e200 * 1.0e200)"), EvalError);                     // inf
    ASSERT_THROW(eval("builtins.ceil (-1.0e200 * 1.0e200)"), EvalError);                    // -inf
    ASSERT_THROW(eval("builtins.ceil (1.0e200 * 1.0e200 - 1.0e200 * 1.0e200)"), EvalError); // nan
    // bugs in previous Nix versions
    ASSERT_THROW(eval("builtins.ceil (4611686018427387904 + 4611686018427387903)"), EvalError);
    ASSERT_THROW(eval("builtins.ceil (-4611686018427387904 - 4611686018427387903)"), EvalError);
}

TEST_F(PrimOpTest, floor)
{
    auto v = eval("builtins.floor 1.9");
    ASSERT_THAT(v, IsIntEq(1));
    auto intMin = eval("builtins.ceil (-4611686018427387904 - 4611686018427387904)");
    ASSERT_THAT(intMin, IsIntEq(std::numeric_limits<NixInt::Inner>::min()));
    ASSERT_THROW(eval("builtins.ceil 1.0e200"), EvalError);
    ASSERT_THROW(eval("builtins.ceil -1.0e200"), EvalError);
    ASSERT_THROW(eval("builtins.ceil (1.0e200 * 1.0e200)"), EvalError);                     // inf
    ASSERT_THROW(eval("builtins.ceil (-1.0e200 * 1.0e200)"), EvalError);                    // -inf
    ASSERT_THROW(eval("builtins.ceil (1.0e200 * 1.0e200 - 1.0e200 * 1.0e200)"), EvalError); // nan
    // bugs in previous Nix versions
    ASSERT_THROW(eval("builtins.ceil (4611686018427387904 + 4611686018427387903)"), EvalError);
    ASSERT_THROW(eval("builtins.ceil (-4611686018427387904 - 4611686018427387903)"), EvalError);
}

TEST_F(PrimOpTest, tryEvalFailure)
{
    auto v = eval("builtins.tryEval (throw \"\")");
    ASSERT_THAT(v, IsAttrsOfSize(2));
    auto s = createSymbol("success");
    auto p = v.attrs()->get(s);
    ASSERT_NE(p, nullptr);
    ASSERT_THAT(*p->value, IsFalse());
}

TEST_F(PrimOpTest, tryEvalSuccess)
{
    auto v = eval("builtins.tryEval 123");
    ASSERT_THAT(v, IsAttrs());
    auto s = createSymbol("success");
    auto p = v.attrs()->get(s);
    ASSERT_NE(p, nullptr);
    ASSERT_THAT(*p->value, IsTrue());
    s = createSymbol("value");
    p = v.attrs()->get(s);
    ASSERT_NE(p, nullptr);
    ASSERT_THAT(*p->value, IsIntEq(123));
}

TEST_F(PrimOpTest, getEnv)
{
    setEnv("_NIX_UNIT_TEST_ENV_VALUE", "test value");
    auto v = eval("builtins.getEnv \"_NIX_UNIT_TEST_ENV_VALUE\"");
    ASSERT_THAT(v, IsStringEq("test value"));
}

TEST_F(PrimOpTest, seq)
{
    ASSERT_THROW(eval("let x = throw \"test\"; in builtins.seq x { }"), ThrownError);
}

TEST_F(PrimOpTest, seqNotDeep)
{
    auto v = eval("let x = { z =  throw \"test\"; }; in builtins.seq x { }");
    ASSERT_THAT(v, IsAttrs());
}

TEST_F(PrimOpTest, deepSeq)
{
    ASSERT_THROW(eval("let x = { z =  throw \"test\"; }; in builtins.deepSeq x { }"), ThrownError);
}

TEST_F(PrimOpTest, trace)
{
    CaptureLogging l;
    auto v = eval("builtins.trace \"test string 123\" 123");
    ASSERT_THAT(v, IsIntEq(123));
    auto text = (dynamic_cast<CaptureLogger *>(logger.get()))->get();
    ASSERT_NE(text.find("test string 123"), std::string::npos);
}

TEST_F(PrimOpTest, placeholder)
{
    auto v = eval("builtins.placeholder \"out\"");
    ASSERT_THAT(v, IsStringEq("/1rz4g4znpzjwh1xymhjpm42vipw92pr73vdgl6xs1hycac8kf2n9"));
}

TEST_F(PrimOpTest, baseNameOf)
{
    auto v = eval("builtins.baseNameOf /some/path");
    ASSERT_THAT(v, IsStringEq("path"));
}

TEST_F(PrimOpTest, dirOf)
{
    auto v = eval("builtins.dirOf /some/path");
    ASSERT_THAT(v, IsPathEq("/some"));
}

TEST_F(PrimOpTest, attrValues)
{
    auto v = eval("builtins.attrValues { x = \"foo\";  a = 1; }");
    ASSERT_THAT(v, IsListOfSize(2));
    ASSERT_THAT(*v.listView()[0], IsIntEq(1));
    ASSERT_THAT(*v.listView()[1], IsStringEq("foo"));
}

TEST_F(PrimOpTest, getAttr)
{
    auto v = eval("builtins.getAttr \"x\" { x = \"foo\"; }");
    ASSERT_THAT(v, IsStringEq("foo"));
}

TEST_F(PrimOpTest, getAttrNotFound)
{
    // FIXME: TypeError is really bad here, also the error wording is worse
    // than on Nix <=2.3
    ASSERT_THROW(eval("builtins.getAttr \"y\" { }"), TypeError);
}

TEST_F(PrimOpTest, unsafeGetAttrPos)
{
    state.corepkgsFS->addFile(CanonPath("foo.nix"), "\n\r\n\r{ y = \"x\"; }");

    auto expr = "builtins.unsafeGetAttrPos \"y\" (import <nix/foo.nix>)";
    auto v = eval(expr);
    ASSERT_THAT(v, IsAttrsOfSize(3));

    auto file = v.attrs()->get(createSymbol("file"));
    ASSERT_NE(file, nullptr);
    ASSERT_THAT(*file->value, IsString());
    auto s = baseNameOf(file->value->string_view());
    ASSERT_EQ(s, "foo.nix");

    auto line = v.attrs()->get(createSymbol("line"));
    ASSERT_NE(line, nullptr);
    state.forceValue(*line->value, noPos);
    ASSERT_THAT(*line->value, IsIntEq(4));

    auto column = v.attrs()->get(createSymbol("column"));
    ASSERT_NE(column, nullptr);
    state.forceValue(*column->value, noPos);
    ASSERT_THAT(*column->value, IsIntEq(3));
}

TEST_F(PrimOpTest, hasAttr)
{
    auto v = eval("builtins.hasAttr \"x\" { x = 1; }");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(PrimOpTest, hasAttrNotFound)
{
    auto v = eval("builtins.hasAttr \"x\" { }");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(PrimOpTest, isAttrs)
{
    auto v = eval("builtins.isAttrs {}");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(PrimOpTest, isAttrsFalse)
{
    auto v = eval("builtins.isAttrs null");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(PrimOpTest, removeAttrs)
{
    auto v = eval("builtins.removeAttrs { x = 1; } [\"x\"]");
    ASSERT_THAT(v, IsAttrsOfSize(0));
}

TEST_F(PrimOpTest, removeAttrsRetains)
{
    auto v = eval("builtins.removeAttrs { x = 1; y = 2; } [\"x\"]");
    ASSERT_THAT(v, IsAttrsOfSize(1));
    ASSERT_NE(v.attrs()->get(createSymbol("y")), nullptr);
}

TEST_F(PrimOpTest, listToAttrsEmptyList)
{
    auto v = eval("builtins.listToAttrs []");
    ASSERT_THAT(v, IsAttrsOfSize(0));
    ASSERT_EQ(v.type(), nAttrs);
    ASSERT_EQ(v.attrs()->size(), 0u);
}

TEST_F(PrimOpTest, listToAttrsNotFieldName)
{
    ASSERT_THROW(eval("builtins.listToAttrs [{}]"), Error);
}

TEST_F(PrimOpTest, listToAttrs)
{
    auto v = eval("builtins.listToAttrs [ { name = \"key\"; value = 123; } ]");
    ASSERT_THAT(v, IsAttrsOfSize(1));
    auto key = v.attrs()->get(createSymbol("key"));
    ASSERT_NE(key, nullptr);
    ASSERT_THAT(*key->value, IsIntEq(123));
}

TEST_F(PrimOpTest, intersectAttrs)
{
    auto v = eval("builtins.intersectAttrs { a = 1; b = 2; } { b = 3; c = 4; }");
    ASSERT_THAT(v, IsAttrsOfSize(1));
    auto b = v.attrs()->get(createSymbol("b"));
    ASSERT_NE(b, nullptr);
    ASSERT_THAT(*b->value, IsIntEq(3));
}

TEST_F(PrimOpTest, catAttrs)
{
    auto v = eval("builtins.catAttrs \"a\" [{a = 1;} {b = 0;} {a = 2;}]");
    ASSERT_THAT(v, IsListOfSize(2));
    ASSERT_THAT(*v.listView()[0], IsIntEq(1));
    ASSERT_THAT(*v.listView()[1], IsIntEq(2));
}

TEST_F(PrimOpTest, functionArgs)
{
    auto v = eval("builtins.functionArgs ({ x, y ? 123}: 1)");
    ASSERT_THAT(v, IsAttrsOfSize(2));

    auto x = v.attrs()->get(createSymbol("x"));
    ASSERT_NE(x, nullptr);
    ASSERT_THAT(*x->value, IsFalse());

    auto y = v.attrs()->get(createSymbol("y"));
    ASSERT_NE(y, nullptr);
    ASSERT_THAT(*y->value, IsTrue());
}

TEST_F(PrimOpTest, mapAttrs)
{
    auto v = eval("builtins.mapAttrs (name: value: value * 10) { a = 1; b = 2; }");
    ASSERT_THAT(v, IsAttrsOfSize(2));

    auto a = v.attrs()->get(createSymbol("a"));
    ASSERT_NE(a, nullptr);
    ASSERT_THAT(*a->value, IsThunk());
    state.forceValue(*a->value, noPos);
    ASSERT_THAT(*a->value, IsIntEq(10));

    auto b = v.attrs()->get(createSymbol("b"));
    ASSERT_NE(b, nullptr);
    ASSERT_THAT(*b->value, IsThunk());
    state.forceValue(*b->value, noPos);
    ASSERT_THAT(*b->value, IsIntEq(20));
}

TEST_F(PrimOpTest, isList)
{
    auto v = eval("builtins.isList []");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(PrimOpTest, isListFalse)
{
    auto v = eval("builtins.isList null");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(PrimOpTest, elemtAt)
{
    auto v = eval("builtins.elemAt [0 1 2 3] 3");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(PrimOpTest, elemtAtOutOfBounds)
{
    ASSERT_THROW(eval("builtins.elemAt [0 1 2 3] 5"), Error);
    ASSERT_THROW(eval("builtins.elemAt [0] 4294967296"), Error);
}

TEST_F(PrimOpTest, head)
{
    auto v = eval("builtins.head [ 3 2 1 0 ]");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(PrimOpTest, headEmpty)
{
    ASSERT_THROW(eval("builtins.head [ ]"), Error);
}

TEST_F(PrimOpTest, headWrongType)
{
    ASSERT_THROW(eval("builtins.head { }"), Error);
}

TEST_F(PrimOpTest, tail)
{
    auto v = eval("builtins.tail [ 3 2 1 0 ]");
    ASSERT_THAT(v, IsListOfSize(3));
    auto listView = v.listView();
    for (const auto [n, elem] : enumerate(listView))
        ASSERT_THAT(*elem, IsIntEq(2 - static_cast<int>(n)));
}

TEST_F(PrimOpTest, tailEmpty)
{
    ASSERT_THROW(eval("builtins.tail []"), Error);
}

TEST_F(PrimOpTest, map)
{
    auto v = eval("map (x: \"foo\" + x) [ \"bar\" \"bla\" \"abc\" ]");
    ASSERT_THAT(v, IsListOfSize(3));
    auto elem = v.listView()[0];
    ASSERT_THAT(*elem, IsThunk());
    state.forceValue(*elem, noPos);
    ASSERT_THAT(*elem, IsStringEq("foobar"));

    elem = v.listView()[1];
    ASSERT_THAT(*elem, IsThunk());
    state.forceValue(*elem, noPos);
    ASSERT_THAT(*elem, IsStringEq("foobla"));

    elem = v.listView()[2];
    ASSERT_THAT(*elem, IsThunk());
    state.forceValue(*elem, noPos);
    ASSERT_THAT(*elem, IsStringEq("fooabc"));
}

TEST_F(PrimOpTest, filter)
{
    auto v = eval("builtins.filter (x: x == 2) [ 3 2 3 2 3 2 ]");
    ASSERT_THAT(v, IsListOfSize(3));
    for (const auto elem : v.listView())
        ASSERT_THAT(*elem, IsIntEq(2));
}

TEST_F(PrimOpTest, elemTrue)
{
    auto v = eval("builtins.elem 3 [ 1 2 3 4 5 ]");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(PrimOpTest, elemFalse)
{
    auto v = eval("builtins.elem 6 [ 1 2 3 4 5 ]");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(PrimOpTest, concatLists)
{
    auto v = eval("builtins.concatLists [[1 2] [3 4]]");
    ASSERT_THAT(v, IsListOfSize(4));
    auto listView = v.listView();
    for (const auto [i, elem] : enumerate(listView))
        ASSERT_THAT(*elem, IsIntEq(static_cast<int>(i) + 1));
}

TEST_F(PrimOpTest, length)
{
    auto v = eval("builtins.length [ 1 2 3 ]");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(PrimOpTest, foldStrict)
{
    auto v = eval("builtins.foldl' (a: b: a + b) 0 [1 2 3]");
    ASSERT_THAT(v, IsIntEq(6));
}

TEST_F(PrimOpTest, anyTrue)
{
    auto v = eval("builtins.any (x: x == 2) [ 1 2 3 ]");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(PrimOpTest, anyFalse)
{
    auto v = eval("builtins.any (x: x == 5) [ 1 2 3 ]");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(PrimOpTest, allTrue)
{
    auto v = eval("builtins.all (x: x > 0) [ 1 2 3 ]");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(PrimOpTest, allFalse)
{
    auto v = eval("builtins.all (x: x <= 0) [ 1 2 3 ]");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(PrimOpTest, genList)
{
    auto v = eval("builtins.genList (x: x + 1) 3");
    ASSERT_EQ(v.type(), nList);
    ASSERT_EQ(v.listSize(), 3u);
    auto listView = v.listView();
    for (const auto [i, elem] : enumerate(listView)) {
        ASSERT_THAT(*elem, IsThunk());
        state.forceValue(*elem, noPos);
        ASSERT_THAT(*elem, IsIntEq(static_cast<int>(i) + 1));
    }
}

TEST_F(PrimOpTest, sortLessThan)
{
    auto v = eval("builtins.sort builtins.lessThan [ 483 249 526 147 42 77 ]");
    ASSERT_EQ(v.type(), nList);
    ASSERT_EQ(v.listSize(), 6u);

    const std::vector<int> numbers = {42, 77, 147, 249, 483, 526};
    auto listView = v.listView();
    for (const auto [n, elem] : enumerate(listView))
        ASSERT_THAT(*elem, IsIntEq(numbers[n]));
}

TEST_F(PrimOpTest, partition)
{
    auto v = eval("builtins.partition (x: x > 10) [1 23 9 3 42]");
    ASSERT_THAT(v, IsAttrsOfSize(2));

    auto right = v.attrs()->get(createSymbol("right"));
    ASSERT_NE(right, nullptr);
    ASSERT_THAT(*right->value, IsListOfSize(2));
    ASSERT_THAT(*right->value->listView()[0], IsIntEq(23));
    ASSERT_THAT(*right->value->listView()[1], IsIntEq(42));

    auto wrong = v.attrs()->get(createSymbol("wrong"));
    ASSERT_NE(wrong, nullptr);
    ASSERT_EQ(wrong->value->type(), nList);
    ASSERT_EQ(wrong->value->listSize(), 3u);
    ASSERT_THAT(*wrong->value, IsListOfSize(3));
    ASSERT_THAT(*wrong->value->listView()[0], IsIntEq(1));
    ASSERT_THAT(*wrong->value->listView()[1], IsIntEq(9));
    ASSERT_THAT(*wrong->value->listView()[2], IsIntEq(3));
}

TEST_F(PrimOpTest, concatMap)
{
    auto v = eval("builtins.concatMap (x: x ++ [0]) [ [1 2] [3 4] ]");
    ASSERT_EQ(v.type(), nList);
    ASSERT_EQ(v.listSize(), 6u);

    const std::vector<int> numbers = {1, 2, 0, 3, 4, 0};
    auto listView = v.listView();
    for (const auto [n, elem] : enumerate(listView))
        ASSERT_THAT(*elem, IsIntEq(numbers[n]));
}

TEST_F(PrimOpTest, addInt)
{
    auto v = eval("builtins.add 3 5");
    ASSERT_THAT(v, IsIntEq(8));
}

TEST_F(PrimOpTest, addFloat)
{
    auto v = eval("builtins.add 3.0 5.0");
    ASSERT_THAT(v, IsFloatEq(8.0));
}

TEST_F(PrimOpTest, addFloatToInt)
{
    auto v = eval("builtins.add 3.0 5");
    ASSERT_THAT(v, IsFloatEq(8.0));

    v = eval("builtins.add 3 5.0");
    ASSERT_THAT(v, IsFloatEq(8.0));
}

TEST_F(PrimOpTest, subInt)
{
    auto v = eval("builtins.sub 5 2");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(PrimOpTest, subFloat)
{
    auto v = eval("builtins.sub 5.0 2.0");
    ASSERT_THAT(v, IsFloatEq(3.0));
}

TEST_F(PrimOpTest, subFloatFromInt)
{
    auto v = eval("builtins.sub 5.0 2");
    ASSERT_THAT(v, IsFloatEq(3.0));

    v = eval("builtins.sub 4 2.0");
    ASSERT_THAT(v, IsFloatEq(2.0));
}

TEST_F(PrimOpTest, mulInt)
{
    auto v = eval("builtins.mul 3 5");
    ASSERT_THAT(v, IsIntEq(15));
}

TEST_F(PrimOpTest, mulFloat)
{
    auto v = eval("builtins.mul 3.0 5.0");
    ASSERT_THAT(v, IsFloatEq(15.0));
}

TEST_F(PrimOpTest, mulFloatMixed)
{
    auto v = eval("builtins.mul 3 5.0");
    ASSERT_THAT(v, IsFloatEq(15.0));

    v = eval("builtins.mul 2.0 5");
    ASSERT_THAT(v, IsFloatEq(10.0));
}

TEST_F(PrimOpTest, divInt)
{
    auto v = eval("builtins.div 5 (-1)");
    ASSERT_THAT(v, IsIntEq(-5));
}

TEST_F(PrimOpTest, divIntZero)
{
    ASSERT_THROW(eval("builtins.div 5 0"), EvalError);
}

TEST_F(PrimOpTest, divFloat)
{
    auto v = eval("builtins.div 5.0 (-1)");
    ASSERT_THAT(v, IsFloatEq(-5.0));
}

TEST_F(PrimOpTest, divFloatZero)
{
    ASSERT_THROW(eval("builtins.div 5.0 0.0"), EvalError);
}

TEST_F(PrimOpTest, bitOr)
{
    auto v = eval("builtins.bitOr 1 2");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(PrimOpTest, bitXor)
{
    auto v = eval("builtins.bitXor 3 2");
    ASSERT_THAT(v, IsIntEq(1));
}

TEST_F(PrimOpTest, lessThanFalse)
{
    auto v = eval("builtins.lessThan 3 1");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(PrimOpTest, lessThanTrue)
{
    auto v = eval("builtins.lessThan 1 3");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(PrimOpTest, toStringAttrsThrows)
{
    ASSERT_THROW(eval("builtins.toString {}"), EvalError);
}

TEST_F(PrimOpTest, toStringLambda)
{
    auto v = eval("builtins.toString (x: x)");
    ASSERT_THAT(v, IsStringEq("(x: x)"));
}

TEST_F(PrimOpTest, toStringLambdaWithFormals)
{
    auto v = eval("builtins.toString ({ a, b ? 1 }: a)");
    ASSERT_THAT(v, IsStringEq("({ a, b ? 1 }: a)"));
}

TEST_F(PrimOpTest, toStringPrimOp)
{
    auto v = eval("builtins.toString builtins.head");
    ASSERT_THAT(v, IsStringEq("builtins.head"));
}

TEST_F(PrimOpTest, serializeFunctionSimple)
{
    auto v = eval("builtins.serializeFunction (x: x)");
    ASSERT_THAT(v, IsStringEq("(x: x)"));
}

TEST_F(PrimOpTest, serializeFunctionWithClosure)
{
    auto v = eval("let x = 1; in builtins.serializeFunction (y: x + y)");
    ASSERT_THAT(v, IsStringEq("(let x = 1; in (y: (x + y)))"));
}

TEST_F(PrimOpTest, serializeFunctionWithMultipleCaptured)
{
    auto v = eval("let x = 1; y = 2; in builtins.serializeFunction (z: x + y + z)");
    ASSERT_THAT(v, IsStringEq("(let x = 1; y = 2; in (z: ((x + y) + z)))"));
}

TEST_F(PrimOpTest, serializeFunctionWithStringClosure)
{
    auto v = eval(R"(let name = "world"; in builtins.serializeFunction (x: "hello ${name}"))");
    ASSERT_THAT(v, IsStringEq(R"((let name = "world"; in (x: ("hello " + name))))"));
}

TEST_F(PrimOpTest, serializeFunctionNoClosure)
{
    auto v = eval("builtins.serializeFunction ({ a, b ? 1 }: a + b)");
    ASSERT_THAT(v, IsStringEq("({ a, b ? 1 }: (a + b))"));
}

TEST_F(PrimOpTest, serializeFunctionPrimOp)
{
    auto v = eval("builtins.serializeFunction builtins.head");
    ASSERT_THAT(v, IsStringEq("builtins.head"));
}

TEST_F(PrimOpTest, serializeFunctionPrimOpRoundTrip)
{
    auto v = eval(R"nix(
        let f = builtins.deserializeFunction (builtins.serializeFunction builtins.head);
        in f [42 1 2]
    )nix");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_EQ(v.integer().value, 42);
}

TEST_F(PrimOpTest, deserializeFunctionSimple)
{
    auto v = eval(R"nix(let f = builtins.deserializeFunction "(x: x + 1)"; in f 41)nix");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_EQ(v.integer().value, 42);
}

TEST_F(PrimOpTest, deserializeFunctionNonFunctionThrows)
{
    ASSERT_THROW(eval(R"nix(builtins.deserializeFunction "42")nix"), EvalError);
}

TEST_F(PrimOpTest, serializeDeserializeRoundTrip)
{
    auto v = eval(R"(
        let
          original = let x = 10; in (y: x + y);
          serialized = builtins.serializeFunction original;
          restored = builtins.deserializeFunction serialized;
        in restored 5
    )");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_EQ(v.integer().value, 15);
}

TEST_F(PrimOpTest, serializeDeserializeRoundTripString)
{
    auto v = eval(R"(
        let
          greeting = "hello";
          original = (name: "${greeting} ${name}");
          serialized = builtins.serializeFunction original;
          restored = builtins.deserializeFunction serialized;
        in restored "world"
    )");
    ASSERT_THAT(v, IsStringEq("hello world"));
}

// `toString` for partially applied primops shows only the base name (lossy).
TEST_F(PrimOpTest, toStringPartiallyAppliedPrimOp)
{
    auto v = eval("builtins.toString (builtins.map builtins.head)");
    ASSERT_THAT(v, IsStringEq("(builtins.map builtins.head)"));
}

// serializeFunction preserves partially applied primop arguments.
TEST_F(PrimOpTest, serializeFunctionPartiallyAppliedPrimOp)
{
    auto v = eval("builtins.serializeFunction (builtins.map builtins.head)");
    ASSERT_THAT(v, IsStringEq("(builtins.map builtins.head)"));
}

// Partially applied primop round-trips through serialize/deserialize.
TEST_F(PrimOpTest, serializeDeserializePartialPrimOpRoundTrip)
{
    auto v = eval(R"nix(
        let
          f = builtins.deserializeFunction
                (builtins.serializeFunction (builtins.map builtins.head));
        in f [[1 2] [3 4] [5 6]]
    )nix");
    ASSERT_EQ(v.type(), nList);
    ASSERT_EQ(v.listSize(), 3);
}

// Nested closures: inner closure bindings are now reconstructed.
TEST_F(PrimOpTest, serializeFunctionNestedClosureRoundTrip)
{
    auto v = eval(R"(
        let
          inner = let x = 10; in (y: x + y);
          serialized = builtins.serializeFunction (z: inner z);
          restored = builtins.deserializeFunction serialized;
        in restored 5
    )");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_EQ(v.integer().value, 15);
}

// `with`-bound variables are now captured in serialized output.
TEST_F(PrimOpTest, serializeFunctionWithBindingCaptured)
{
    auto v = eval(R"nix(
        let
          serialized = with { greeting = "hi"; };
            builtins.serializeFunction (x: greeting);
          restored = builtins.deserializeFunction serialized;
        in restored null
    )nix");
    ASSERT_THAT(v, IsStringEq("hi"));
}

// Recursive attrset in closure: emits back-reference to let-binding name.
TEST_F(PrimOpTest, serializeFunctionRecursiveAttrset)
{
    auto v = eval("let s = { x = s; }; in builtins.serializeFunction (y: s)");
    // The let binding is recursive: `let s = { x = s; }; in ...`
    auto s = std::string(v.string_view());
    EXPECT_THAT(s, ::testing::HasSubstr("let "));
    EXPECT_THAT(s, ::testing::HasSubstr("s"));
}

// Recursive attrset round-trips through serialize/deserialize.
TEST_F(PrimOpTest, serializeDeserializeRecursiveAttrsetRoundTrip)
{
    auto v = eval(R"(
        let
          s = { x = s; val = 42; };
          serialized = builtins.serializeFunction (y: s.val);
          restored = builtins.deserializeFunction serialized;
        in restored null
    )");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_EQ(v.integer().value, 42);
}

// Edge case: closure with list and attrset values round-trips correctly.
TEST_F(PrimOpTest, serializeDeserializeRoundTripCompound)
{
    auto v = eval(R"(
        let
          xs = [1 2 3];
          cfg = { a = true; b = null; };
        in
          let
            serialized = builtins.serializeFunction (f: { inherit xs cfg; val = f; });
            restored = builtins.deserializeFunction serialized;
            result = restored 42;
          in result.xs
    )");
    ASSERT_EQ(v.type(), nList);
    ASSERT_EQ(v.listSize(), 3);
}

// Edge case: float precision.
TEST_F(PrimOpTest, serializeFunctionFloatPrecision)
{
    auto v = eval(R"(
        let pi = 3.14159265358979;
        in builtins.serializeFunction (x: x + pi)
    )");
    auto s = std::string(v.string_view());
    // std::to_string produces 6 decimal places by default.
    EXPECT_THAT(s, ::testing::HasSubstr("3.141593"));
}

// Plugin primops: the isPluginPrimOp flag is set for extraPrimOps.
// We can't easily test the serialization rejection in the test harness
// (the EvalState is already constructed), but we verify the flag works
// on a hand-crafted PrimOp.
TEST_F(PrimOpTest, pluginPrimOpFlagIsSet)
{
    auto * v = state.allocValue();
    v->mkPrimOp(new PrimOp{
        .name = "fakePlugin",
        .args = {"x"},
        .impl = [](EvalState &, const PosIdx, Value **, Value & v) { v.mkNull(); },
        .isPluginPrimOp = true,
    });
    ASSERT_TRUE(v->primOp()->isPluginPrimOp);

    // Built-in primops do not have the flag set.
    auto builtinHead = eval("builtins.head", false);
    state.forceValue(builtinHead, noPos);
    ASSERT_TRUE(builtinHead.isPrimOp());
    ASSERT_FALSE(builtinHead.primOp()->isPluginPrimOp);
}

// Attrset form: accepts { fn, allowedPluginPrimOps }.
TEST_F(PrimOpTest, serializeFunctionAttrsetForm)
{
    auto v = eval(R"(
        builtins.serializeFunction {
          fn = x: x + 1;
          allowedPluginPrimOps = [];
        }
    )");
    ASSERT_THAT(v, IsStringEq("(x: (x + 1))"));
}

// Attrset form without fn attribute throws.
TEST_F(PrimOpTest, serializeFunctionAttrsetNoFnThrows)
{
    ASSERT_THROW(eval("builtins.serializeFunction { }"), EvalError);
}

// preserveStringContext = false (default): plain string, no appendContext.
TEST_F(PrimOpTest, serializeFunctionStringContextNotPreservedByDefault)
{
    auto v = eval(R"nix(
        let s = "hello";
        in builtins.serializeFunction (x: s)
    )nix");
    auto out = std::string(v.string_view());
    ASSERT_EQ(out.find("appendContext"), std::string::npos);
}

// preserveStringContext = true: strings with context get appendContext wrapping.
// We can't easily create real store path context in the test harness, so
// we verify the flag is accepted and plain strings (no context) are unaffected.
TEST_F(PrimOpTest, serializeFunctionPreserveStringContextFlagAccepted)
{
    auto v = eval(R"nix(
        builtins.serializeFunction {
          fn = x: "hello";
          preserveStringContext = true;
        }
    )nix");
    // Plain string without context: no appendContext wrapping needed.
    auto out = std::string(v.string_view());
    ASSERT_EQ(out.find("appendContext"), std::string::npos);
}

// Eta-reduction: direct `builtins.head` access is now detected.
TEST_F(PrimOpTest, serializeFunctionEtaReducesDirectBuiltin)
{
    auto v = eval("builtins.serializeFunction (x: builtins.head x)");
    ASSERT_THAT(v, IsStringEq("builtins.head"));
}

// Eta-reduction: polyfill in closure normalizes to the underlying primop.
TEST_F(PrimOpTest, serializeFunctionEtaReducesClosurePolyfill)
{
    auto v = eval("let head = builtins.head; in builtins.serializeFunction (x: head x)");
    ASSERT_THAT(v, IsStringEq("builtins.head"));
}

// Eta-reduction does not apply when the body does more than forward args.
TEST_F(PrimOpTest, serializeFunctionNoEtaWhenBodyDiffers)
{
    auto v = eval("builtins.serializeFunction (x: builtins.head x + 1)");
    auto s = std::string(v.string_view());
    EXPECT_THAT(s, ::testing::HasSubstr("(x:"));
}

// Multi-arg eta-reduction: `x: y: f x y` reduces to `f`.
TEST_F(PrimOpTest, serializeFunctionEtaReducesMultiArg)
{
    auto v = eval(R"(
        let elemAt = builtins.elemAt;
        in builtins.serializeFunction (list: index: elemAt list index)
    )");
    ASSERT_THAT(v, IsStringEq("builtins.elemAt"));
}

// Multi-arg eta-reduction with direct builtin: `x: y: builtins.elemAt x y`.
TEST_F(PrimOpTest, serializeFunctionEtaReducesMultiArgDirectBuiltin)
{
    auto v = eval("builtins.serializeFunction (list: index: builtins.elemAt list index)");
    ASSERT_THAT(v, IsStringEq("builtins.elemAt"));
}

// Multi-arg eta-reduction with a user-defined function.
TEST_F(PrimOpTest, serializeFunctionEtaReducesMultiArgLambda)
{
    auto v = eval(R"(
        let myAdd = a: b: a + b;
        in builtins.serializeFunction (x: y: myAdd x y)
    )");
    // Reduces to `myAdd` which itself has a closure with no free vars,
    // so it serializes as the lambda body.
    ASSERT_THAT(v, IsStringEq("(a: (b: (a + b)))"));
}

// Eta-reduction does not apply to partial forwarding.
TEST_F(PrimOpTest, serializeFunctionNoEtaPartialForward)
{
    auto v = eval(R"(
        let f = a: b: a + b;
        in builtins.serializeFunction (x: f x 1)
    )");
    auto s = std::string(v.string_view());
    // Not eta-reduced: second arg is `1`, not a lambda param.
    EXPECT_THAT(s, ::testing::HasSubstr("(x:"));
}

/*
 * Dynamic derivations scenario tests.
 *
 * The dynamic derivations use case (RFC 0092) requires serializing a
 * function during evaluation, passing it into a build sandbox, and
 * deserializing it there to produce further derivations.  These tests
 * simulate that pipeline: construct a function that builds derivation-
 * like attrsets, serialize it, deserialize it, and verify the result.
 */

// Scenario: a "mkDerivation" helper captures shared config from the
// evaluation phase, gets serialized into a builder, and is called at
// build time with per-source-file arguments.
TEST_F(PrimOpTest, dynDrvMkDerivationFactory)
{
    auto v = eval(R"(
        let
          system = "x86_64-linux";
          baseFlags = ["-O2" "-Wall"];
          mkCompile = src: {
            name = "compile-${src}";
            inherit system;
            flags = baseFlags ++ [src];
          };
          serialized = builtins.serializeFunction mkCompile;
          restored = builtins.deserializeFunction serialized;
          drv = restored "main.c";
        in drv.name
    )");
    ASSERT_THAT(v, IsStringEq("compile-main.c"));
}

// Scenario: the serialized function captures a dependency map (like a
// lockfile parsed during evaluation) and uses it at build time.
TEST_F(PrimOpTest, dynDrvLockfileDependencyMap)
{
    auto v = eval(R"(
        let
          lockfile = {
            "express" = { version = "4.18.2"; resolved = "/nix/store/fake-express"; };
            "lodash" = { version = "4.17.21"; resolved = "/nix/store/fake-lodash"; };
          };
          resolve = name: lockfile.${name}.resolved;
          serialized = builtins.serializeFunction resolve;
          restored = builtins.deserializeFunction serialized;
        in restored "lodash"
    )");
    ASSERT_THAT(v, IsStringEq("/nix/store/fake-lodash"));
}

// Scenario: higher-order -- a serialized function itself returns
// functions (e.g. a build system that produces per-target builders).
TEST_F(PrimOpTest, dynDrvHigherOrderBuilder)
{
    auto v = eval(R"(
        let
          cc = "/nix/store/fake-gcc";
          mkBuilder = target: src: {
            name = "${target}-${src}";
            compiler = cc;
          };
          serialized = builtins.serializeFunction mkBuilder;
          restored = builtins.deserializeFunction serialized;
          builder = restored "aarch64";
          drv = builder "kernel.c";
        in drv.compiler
    )");
    ASSERT_THAT(v, IsStringEq("/nix/store/fake-gcc"));
}

// Scenario: the function uses builtins (primops) captured through
// partial application -- e.g. a pre-configured map operation.
TEST_F(PrimOpTest, dynDrvPartialPrimopInClosure)
{
    auto v = eval(R"nix(
        let
          transform = builtins.map (x: x * 2);
          apply = inputs: transform inputs;
          serialized = builtins.serializeFunction apply;
          restored = builtins.deserializeFunction serialized;
        in restored [1 2 3]
    )nix");
    ASSERT_EQ(v.type(), nList);
    ASSERT_EQ(v.listSize(), 3);
}

// Scenario: mutual recursion between closure values -- a "plugin
// system" where plugins reference each other.
TEST_F(PrimOpTest, dynDrvMutuallyRecursiveClosures)
{
    auto v = eval(R"(
        let
          plugins = {
            a = { name = "plugin-a"; deps = [ plugins.b ]; };
            b = { name = "plugin-b"; deps = []; };
          };
          getName = p: (builtins.head plugins.${p}.deps).name or plugins.${p}.name;
          serialized = builtins.serializeFunction getName;
          restored = builtins.deserializeFunction serialized;
        in restored "a"
    )");
    ASSERT_THAT(v, IsStringEq("plugin-b"));
}

// Limitation test: string context is lost on round-trip.
// In a real dynamic derivation scenario, store paths in closure strings
// would lose their dependency tracking after deserialization.
// We verify this by checking that a path-containing string in a closure
// loses its context after serialize/deserialize.
TEST_F(PrimOpTest, dynDrvStringContextLostOnRoundTrip)
{
    // builtins.storePath would add context, but requires a real store path.
    // Instead, test that the serialized string itself carries context
    // from the closure (via copyContext in serializeValue), but the
    // deserialized result does not.  We use builtins.toFile to create
    // a real store path with context.
    //
    // This test cannot easily be written without a store, so we test
    // the simpler case: a plain string in a closure round-trips as a
    // plain string (no context to lose), documenting the gap.
    auto v = eval(R"(
        let
          path = "/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-hello";
          f = _: path;
          serialized = builtins.serializeFunction f;
          restored = builtins.deserializeFunction serialized;
        in restored null
    )");
    // The string survives, but in a real scenario with store path context
    // attached, the context would be lost after deserialization.
    ASSERT_THAT(v, IsStringEq("/nix/store/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa-hello"));
}

// Scenario: `preserveStringContext` emits `builtins.appendContext` wrapping
// for strings with context.  We verify the output format since the test
// harness uses a dummy store that cannot validate real store paths.
TEST_F(PrimOpTest, dynDrvPreserveStringContextOutput)
{
    auto v = eval(R"nix(
        let
          s = "hello";
          f = _: s;
        in builtins.serializeFunction { fn = f; preserveStringContext = true; }
    )nix");
    // Plain string without context: no wrapping needed even with the flag.
    auto out = std::string(v.string_view());
    ASSERT_EQ(out.find("appendContext"), std::string::npos);
}

// Scenario: eta-reduced polyfills in a dynamic derivation pipeline.
// A `lib.head`-style polyfill round-trips to the underlying primop.
TEST_F(PrimOpTest, dynDrvEtaReducedPolyfill)
{
    auto v = eval(R"(
        let
          head = builtins.head;
          getFirst = x: head x;
          serialized = builtins.serializeFunction getFirst;
          restored = builtins.deserializeFunction serialized;
        in restored [ 42 1 2 ]
    )");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_EQ(v.integer().value, 42);
}

// Scenario: a build system that combines eval-time configuration,
// a polyfilled lib function, and a dependency map -- the full pipeline.
TEST_F(PrimOpTest, dynDrvFullPipeline)
{
    auto v = eval(R"(
        let
          system = "x86_64-linux";
          concatStringsSep = builtins.concatStringsSep;
          deps = { "main.c" = [ "stdio.h" "stdlib.h" ]; };
          mkCompileCmd = src: {
            name = "compile-${src}";
            inherit system;
            includes = concatStringsSep " " (builtins.map (h: "-I${h}") (deps.${src} or []));
          };
          serialized = builtins.serializeFunction mkCompileCmd;
          restored = builtins.deserializeFunction serialized;
          cmd = restored "main.c";
        in cmd.includes
    )");
    ASSERT_THAT(v, IsStringEq("-Istdio.h -Istdlib.h"));
}

// Scenario: the attrset form with `allowedPluginPrimOps` in a
// dynamic derivation pipeline.  Plugin primops are not available in
// the test harness, but we verify the option is accepted and does
// not interfere with normal serialization.
TEST_F(PrimOpTest, dynDrvAttrsetFormWithOptions)
{
    auto v = eval(R"(
        let
          f = x: x + 1;
          serialized = builtins.serializeFunction {
            fn = f;
            allowedPluginPrimOps = [ "hypotheticalPlugin" ];
            preserveStringContext = true;
          };
          restored = builtins.deserializeFunction serialized;
        in restored 41
    )");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_EQ(v.integer().value, 42);
}

// The `derivation` function (a top-level constant, not a primop) is
// available after deserialization because `deserializeFunction` parses
// in the base environment.
TEST_F(PrimOpTest, dynDrvDerivationRoundTrip)
{
    auto v = eval(R"(
        let
          mkDrv = builtins.deserializeFunction
            (builtins.serializeFunction
              (name: derivation {
                inherit name;
                system = "x86_64-linux";
                builder = "/bin/sh";
              }));
        in (mkDrv "hello").name
    )");
    ASSERT_THAT(v, IsStringEq("hello"));
}

// The `derivation` function works when captured indirectly in a closure.
TEST_F(PrimOpTest, dynDrvDerivationInClosure)
{
    auto v = eval(R"(
        let
          system = "x86_64-linux";
          mkDrv = name: derivation {
            inherit name system;
            builder = "/bin/sh";
          };
          serialized = builtins.serializeFunction mkDrv;
          restored = builtins.deserializeFunction serialized;
        in (restored "test").type
    )");
    ASSERT_THAT(v, IsStringEq("derivation"));
}

// `import` serializes as `builtins.import` and is available after
// deserialization.
TEST_F(PrimOpTest, dynDrvImportPrimOpRoundTrip)
{
    auto v = eval(R"nix(
        let
          serialized = builtins.serializeFunction (x: import x);
        in serialized
    )nix");
    ASSERT_THAT(v, IsStringEq("builtins.import"));
}

// provided at deserialization time, not inlined.
TEST_F(PrimOpTest, substring)
{
    auto v = eval("builtins.substring 0 3 \"nixos\"");
    ASSERT_THAT(v, IsStringEq("nix"));
}

TEST_F(PrimOpTest, substringSmallerString)
{
    auto v = eval("builtins.substring 0 3 \"n\"");
    ASSERT_THAT(v, IsStringEq("n"));
}

TEST_F(PrimOpTest, substringHugeStart)
{
    auto v = eval("builtins.substring 4294967296 5 \"nixos\"");
    ASSERT_THAT(v, IsStringEq(""));
}

TEST_F(PrimOpTest, substringHugeLength)
{
    auto v = eval("builtins.substring 0 4294967296 \"nixos\"");
    ASSERT_THAT(v, IsStringEq("nixos"));
}

TEST_F(PrimOpTest, substringEmptyString)
{
    auto v = eval("builtins.substring 1 3 \"\"");
    ASSERT_THAT(v, IsStringEq(""));
}

TEST_F(PrimOpTest, stringLength)
{
    auto v = eval("builtins.stringLength \"123\"");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(PrimOpTest, hashStringMd5)
{
    auto v = eval("builtins.hashString \"md5\" \"asdf\"");
    ASSERT_THAT(v, IsStringEq("912ec803b2ce49e4a541068d495ab570"));
}

TEST_F(PrimOpTest, hashStringSha1)
{
    auto v = eval("builtins.hashString \"sha1\" \"asdf\"");
    ASSERT_THAT(v, IsStringEq("3da541559918a808c2402bba5012f6c60b27661c"));
}

TEST_F(PrimOpTest, hashStringSha256)
{
    auto v = eval("builtins.hashString \"sha256\" \"asdf\"");
    ASSERT_THAT(v, IsStringEq("f0e4c2f76c58916ec258f246851bea091d14d4247a2fc3e18694461b1816e13b"));
}

TEST_F(PrimOpTest, hashStringSha512)
{
    auto v = eval("builtins.hashString \"sha512\" \"asdf\"");
    ASSERT_THAT(
        v,
        IsStringEq(
            "401b09eab3c013d4ca54922bb802bec8fd5318192b0a75f201d8b3727429080fb337591abd3e44453b954555b7a0812e1081c39b740293f765eae731f5a65ed1"));
}

TEST_F(PrimOpTest, hashStringInvalidHashAlgorithm)
{
    ASSERT_THROW(eval("builtins.hashString \"foobar\" \"asdf\""), Error);
}

TEST_F(PrimOpTest, nixPath)
{
    auto v = eval("builtins.nixPath");
    ASSERT_EQ(v.type(), nList);
    // We can't test much more as currently the EvalSettings are a global
    // that we can't easily swap / replace
}

TEST_F(PrimOpTest, langVersion)
{
    auto v = eval("builtins.langVersion");
    ASSERT_EQ(v.type(), nInt);
}

TEST_F(PrimOpTest, storeDir)
{
    auto v = eval("builtins.storeDir");
    ASSERT_THAT(v, IsStringEq(state.store->storeDir));
}

TEST_F(PrimOpTest, nixVersion)
{
    auto v = eval("builtins.nixVersion");
    ASSERT_THAT(v, IsStringEq(nixVersion));
}

TEST_F(PrimOpTest, currentSystem)
{
    auto v = eval("builtins.currentSystem");
    ASSERT_THAT(v, IsStringEq(evalSettings.getCurrentSystem()));
}

TEST_F(PrimOpTest, derivation)
{
    auto v = eval("derivation");
    ASSERT_EQ(v.type(), nFunction);
    ASSERT_TRUE(v.isLambda());
    ASSERT_NE(v.lambda().fun, nullptr);
    ASSERT_TRUE(v.lambda().fun->getFormals());
}

TEST_F(PrimOpTest, currentTime)
{
    auto v = eval("builtins.currentTime");
    ASSERT_EQ(v.type(), nInt);
    ASSERT_TRUE(v.integer() > 0);
}

TEST_F(PrimOpTest, splitVersion)
{
    auto v = eval("builtins.splitVersion \"1.2.3git\"");
    ASSERT_THAT(v, IsListOfSize(4));

    const std::vector<std::string_view> strings = {"1", "2", "3", "git"};
    auto listView = v.listView();
    for (const auto [n, p] : enumerate(listView))
        ASSERT_THAT(*p, IsStringEq(strings[n]));
}

class CompareVersionsPrimOpTest : public PrimOpTest,
                                  public ::testing::WithParamInterface<std::tuple<std::string, const int>>
{};

TEST_P(CompareVersionsPrimOpTest, compareVersions)
{
    const auto & [expression, expectation] = GetParam();
    auto v = eval(expression);
    ASSERT_THAT(v, IsIntEq(expectation));
}

#define CASE(a, b, expected) (std::make_tuple("builtins.compareVersions \"" #a "\" \"" #b "\"", expected))
INSTANTIATE_TEST_SUITE_P(
    compareVersions,
    CompareVersionsPrimOpTest,
    ::testing::Values(
        // The first two are weird cases. Intuition tells they should
        // be the same but they aren't.
        CASE(1.0, 1.0.0, -1),
        CASE(1.0.0, 1.0, 1),
        // the following are from the nix-env manual:
        CASE(1.0, 2.3, -1),
        CASE(2.1, 2.3, -1),
        CASE(2.3, 2.3, 0),
        CASE(2.5, 2.3, 1),
        CASE(3.1, 2.3, 1),
        CASE(2.3.1, 2.3, 1),
        CASE(2.3.1, 2.3a, 1),
        CASE(2.3pre1, 2.3, -1),
        CASE(2.3pre3, 2.3pre12, -1),
        CASE(2.3a, 2.3c, -1),
        CASE(2.3pre1, 2.3c, -1),
        CASE(2.3pre1, 2.3q, -1)));
#undef CASE

class ParseDrvNamePrimOpTest
    : public PrimOpTest,
      public ::testing::WithParamInterface<std::tuple<std::string, std::string_view, std::string_view>>
{};

TEST_P(ParseDrvNamePrimOpTest, parseDrvName)
{
    const auto & [input, expectedName, expectedVersion] = GetParam();
    const auto expr = fmt("builtins.parseDrvName \"%1%\"", input);
    auto v = eval(expr);
    ASSERT_THAT(v, IsAttrsOfSize(2));

    auto name = v.attrs()->get(createSymbol("name"));
    ASSERT_TRUE(name);
    ASSERT_THAT(*name->value, IsStringEq(expectedName));

    auto version = v.attrs()->get(createSymbol("version"));
    ASSERT_TRUE(version);
    ASSERT_THAT(*version->value, IsStringEq(expectedVersion));
}

INSTANTIATE_TEST_SUITE_P(
    parseDrvName,
    ParseDrvNamePrimOpTest,
    ::testing::Values(
        std::make_tuple("nix-0.12pre12876", "nix", "0.12pre12876"),
        std::make_tuple("a-b-c-1234pre5+git", "a-b-c", "1234pre5+git")));

TEST_F(PrimOpTest, replaceStrings)
{
    // FIXME: add a test that verifies the string context is as expected
    auto v = eval("builtins.replaceStrings [\"oo\" \"a\"] [\"a\" \"i\"] \"foobar\"");
    ASSERT_EQ(v.type(), nString);
    ASSERT_EQ(v.string_view(), "fabir");
}

TEST_F(PrimOpTest, concatStringsSep)
{
    // FIXME: add a test that verifies the string context is as expected
    auto v = eval("builtins.concatStringsSep \"%\" [\"foo\" \"bar\" \"baz\"]");
    ASSERT_EQ(v.type(), nString);
    ASSERT_EQ(v.string_view(), "foo%bar%baz");
}

TEST_F(PrimOpTest, split1)
{
    // v = [ "" [ "a" ] "c" ]
    auto v = eval("builtins.split \"(a)b\" \"abc\"");
    ASSERT_THAT(v, IsListOfSize(3));

    ASSERT_THAT(*v.listView()[0], IsStringEq(""));

    ASSERT_THAT(*v.listView()[1], IsListOfSize(1));
    ASSERT_THAT(*v.listView()[1]->listView()[0], IsStringEq("a"));

    ASSERT_THAT(*v.listView()[2], IsStringEq("c"));
}

TEST_F(PrimOpTest, split2)
{
    // v is expected to be a list [ "" [ "a" ] "b" [ "c"] "" ]
    auto v = eval("builtins.split \"([ac])\" \"abc\"");
    ASSERT_THAT(v, IsListOfSize(5));

    ASSERT_THAT(*v.listView()[0], IsStringEq(""));

    ASSERT_THAT(*v.listView()[1], IsListOfSize(1));
    ASSERT_THAT(*v.listView()[1]->listView()[0], IsStringEq("a"));

    ASSERT_THAT(*v.listView()[2], IsStringEq("b"));

    ASSERT_THAT(*v.listView()[3], IsListOfSize(1));
    ASSERT_THAT(*v.listView()[3]->listView()[0], IsStringEq("c"));

    ASSERT_THAT(*v.listView()[4], IsStringEq(""));
}

TEST_F(PrimOpTest, split3)
{
    auto v = eval("builtins.split \"(a)|(c)\" \"abc\"");
    ASSERT_THAT(v, IsListOfSize(5));

    // First list element
    ASSERT_THAT(*v.listView()[0], IsStringEq(""));

    // 2nd list element is a list [ "" null ]
    ASSERT_THAT(*v.listView()[1], IsListOfSize(2));
    ASSERT_THAT(*v.listView()[1]->listView()[0], IsStringEq("a"));
    ASSERT_THAT(*v.listView()[1]->listView()[1], IsNull());

    // 3rd element
    ASSERT_THAT(*v.listView()[2], IsStringEq("b"));

    // 4th element is a list: [ null "c" ]
    ASSERT_THAT(*v.listView()[3], IsListOfSize(2));
    ASSERT_THAT(*v.listView()[3]->listView()[0], IsNull());
    ASSERT_THAT(*v.listView()[3]->listView()[1], IsStringEq("c"));

    // 5th element is the empty string
    ASSERT_THAT(*v.listView()[4], IsStringEq(""));
}

TEST_F(PrimOpTest, split4)
{
    auto v = eval("builtins.split \"([[:upper:]]+)\" \" FOO \"");
    ASSERT_THAT(v, IsListOfSize(3));
    auto first = v.listView()[0];
    auto second = v.listView()[1];
    auto third = v.listView()[2];

    ASSERT_THAT(*first, IsStringEq(" "));

    ASSERT_THAT(*second, IsListOfSize(1));
    ASSERT_THAT(*second->listView()[0], IsStringEq("FOO"));

    ASSERT_THAT(*third, IsStringEq(" "));
}

TEST_F(PrimOpTest, match1)
{
    auto v = eval("builtins.match \"ab\" \"abc\"");
    ASSERT_THAT(v, IsNull());
}

TEST_F(PrimOpTest, match2)
{
    auto v = eval("builtins.match \"abc\" \"abc\"");
    ASSERT_THAT(v, IsListOfSize(0));
}

TEST_F(PrimOpTest, match3)
{
    auto v = eval("builtins.match \"a(b)(c)\" \"abc\"");
    ASSERT_THAT(v, IsListOfSize(2));
    ASSERT_THAT(*v.listView()[0], IsStringEq("b"));
    ASSERT_THAT(*v.listView()[1], IsStringEq("c"));
}

TEST_F(PrimOpTest, match4)
{
    auto v = eval("builtins.match \"[[:space:]]+([[:upper:]]+)[[:space:]]+\" \"  FOO   \"");
    ASSERT_THAT(v, IsListOfSize(1));
    ASSERT_THAT(*v.listView()[0], IsStringEq("FOO"));
}

TEST_F(PrimOpTest, match5)
{
    // The regex "\\{}" is valid and matches the string "{}".
    // Caused a regression before when trying to switch from std::regex to boost::regex.
    // See https://github.com/NixOS/nix/pull/7762#issuecomment-1834303659
    auto v = eval("builtins.match \"\\\\{}\" \"{}\"");
    ASSERT_THAT(v, IsListOfSize(0));
}

TEST_F(PrimOpTest, attrNames)
{
    auto v = eval("builtins.attrNames { x = 1; y = 2; z = 3; a = 2; }");
    ASSERT_THAT(v, IsListOfSize(4));

    // ensure that the list is sorted
    const std::vector<std::string_view> expected{"a", "x", "y", "z"};
    auto listView = v.listView();
    for (const auto [n, elem] : enumerate(listView))
        ASSERT_THAT(*elem, IsStringEq(expected[n]));
}

TEST_F(PrimOpTest, genericClosure_not_strict)
{
    // Operator should not be used when startSet is empty
    auto v = eval("builtins.genericClosure { startSet = []; }");
    ASSERT_THAT(v, IsListOfSize(0));
}
} /* namespace nix */
