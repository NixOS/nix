#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/eval-settings.hh"
#include "nix/util/memory-source-accessor.hh"

#include "nix/expr/tests/libexpr.hh"

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

TEST_F(PrimOpTest, toStringLambdaThrows)
{
    ASSERT_THROW(eval("builtins.toString (x: x)"), EvalError);
}

class ToStringPrimOpTest : public PrimOpTest,
                           public testing::WithParamInterface<std::tuple<std::string, std::string_view>>
{};

TEST_P(ToStringPrimOpTest, toString)
{
    const auto & [input, output] = GetParam();
    auto v = eval(input);
    ASSERT_THAT(v, IsStringEq(output));
}

#define CASE(input, output) (std::make_tuple(std::string_view("builtins.toString " input), std::string_view(output)))
INSTANTIATE_TEST_SUITE_P(
    toString,
    ToStringPrimOpTest,
    testing::Values(
        CASE(R"("foo")", "foo"),
        CASE(R"(1)", "1"),
        CASE(R"([1 2 3])", "1 2 3"),
        CASE(R"(.123)", "0.123000"),
        CASE(R"(true)", "1"),
        CASE(R"(false)", ""),
        CASE(R"(null)", ""),
        CASE(R"({ v = "bar"; __toString = self: self.v; })", "bar"),
        CASE(R"({ v = "bar"; __toString = self: self.v; outPath = "foo"; })", "bar"),
        CASE(R"({ outPath = "foo"; })", "foo")
// this is broken on cygwin because canonPath("//./test", false) returns //./test
// FIXME: don't use canonPath
#ifndef __CYGWIN__
            ,
        CASE(R"(./test)", "/test")
#endif
            ));
#undef CASE

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
    ASSERT_THAT(v, IsStringEq(settings.nixStore));
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
                                  public testing::WithParamInterface<std::tuple<std::string, const int>>
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
    testing::Values(
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
      public testing::WithParamInterface<std::tuple<std::string, std::string_view, std::string_view>>
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
    testing::Values(
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

TEST_F(PrimOpTest, genericClosure_dedup_string_keys_by_content)
{
    /* Real-world inspired: nixpkgs commonly uses store paths / names as string
       keys, and we need to deduplicate by string contents (not pointer
       identity). */
    auto v = eval(R"(
      builtins.genericClosure {
        startSet = [ { key = "a"; id = "start"; } ];
        operator = x:
          if x.id == "start" then [
            { key = "a"; id = "dup-a"; }
            { key = "b"; id = "b"; }
            { key = "b"; id = "dup-b"; }
          ] else [ ];
      }
    )");
    ASSERT_THAT(v, IsListOfSize(2));

    auto listView = v.listView();
    ASSERT_THAT(*listView[0], IsAttrs());
    ASSERT_THAT(*listView[1], IsAttrs());
    const auto keySym = createSymbol("key");
    const auto idSym = createSymbol("id");

    auto * key0 = listView[0]->attrs()->get(keySym);
    ASSERT_NE(key0, nullptr);
    ASSERT_THAT(*key0->value, IsStringEq("a"));
    auto * id0 = listView[0]->attrs()->get(idSym);
    ASSERT_NE(id0, nullptr);
    ASSERT_THAT(*id0->value, IsStringEq("start"));

    auto * key1 = listView[1]->attrs()->get(keySym);
    ASSERT_NE(key1, nullptr);
    ASSERT_THAT(*key1->value, IsStringEq("b"));
    auto * id1 = listView[1]->attrs()->get(idSym);
    ASSERT_NE(id1, nullptr);
    ASSERT_THAT(*id1->value, IsStringEq("b"));
}

TEST_F(PrimOpTest, genericClosure_dedup_int_then_float)
{
    /* Regression test for int→float mixing: CompareValues supports comparing
       int keys with float keys, so 1 and 1.0 must be treated as the same key.
       This specifically exercises the int→fallback promotion path. */
    auto v = eval(R"(
      builtins.genericClosure {
        startSet = [ { key = 1; id = "start"; } ];
        operator = x:
          if x.id == "start" then [
            { key = 1.0; id = "dup-float1"; }
            { key = 1; id = "dup-int1"; }
            { key = 2; id = "int2"; }
            { key = 2.0; id = "dup-float2"; }
          ] else [ ];
      }
    )");
    ASSERT_THAT(v, IsListOfSize(2));

    auto listView = v.listView();
    ASSERT_THAT(*listView[0], IsAttrs());
    ASSERT_THAT(*listView[1], IsAttrs());
    const auto keySym = createSymbol("key");
    const auto idSym = createSymbol("id");

    auto * key0 = listView[0]->attrs()->get(keySym);
    ASSERT_NE(key0, nullptr);
    ASSERT_THAT(*key0->value, IsIntEq(1));
    auto * id0 = listView[0]->attrs()->get(idSym);
    ASSERT_NE(id0, nullptr);
    ASSERT_THAT(*id0->value, IsStringEq("start"));

    auto * key1 = listView[1]->attrs()->get(keySym);
    ASSERT_NE(key1, nullptr);
    ASSERT_THAT(*key1->value, IsIntEq(2));
    auto * id1 = listView[1]->attrs()->get(idSym);
    ASSERT_NE(id1, nullptr);
    ASSERT_THAT(*id1->value, IsStringEq("int2"));
}

TEST_F(PrimOpTest, genericClosure_dedup_float_then_int)
{
    /* Regression test for float→int mixing. Starting with a float should use
       the fallback key mode from the beginning, but still deduplicate 1.0 and
       1 (and similarly 2.0 and 2). */
    auto v = eval(R"(
      builtins.genericClosure {
        startSet = [ { key = 1.0; id = "start"; } ];
        operator = x:
          if x.id == "start" then [
            { key = 1; id = "dup-int1"; }
            { key = 2.0; id = "float2"; }
            { key = 2; id = "dup-int2"; }
          ] else [ ];
      }
    )");
    ASSERT_THAT(v, IsListOfSize(2));

    auto listView = v.listView();
    ASSERT_THAT(*listView[0], IsAttrs());
    ASSERT_THAT(*listView[1], IsAttrs());
    const auto keySym = createSymbol("key");
    const auto idSym = createSymbol("id");

    auto * key0 = listView[0]->attrs()->get(keySym);
    ASSERT_NE(key0, nullptr);
    ASSERT_THAT(*key0->value, IsFloatEq(1.0));
    auto * id0 = listView[0]->attrs()->get(idSym);
    ASSERT_NE(id0, nullptr);
    ASSERT_THAT(*id0->value, IsStringEq("start"));

    auto * key1 = listView[1]->attrs()->get(keySym);
    ASSERT_NE(key1, nullptr);
    ASSERT_THAT(*key1->value, IsFloatEq(2.0));
    auto * id1 = listView[1]->attrs()->get(idSym);
    ASSERT_NE(id1, nullptr);
    ASSERT_THAT(*id1->value, IsStringEq("float2"));
}
} /* namespace nix */
