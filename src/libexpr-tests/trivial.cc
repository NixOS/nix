#include "nix/expr/tests/libexpr.hh"
#include "nix/util/tests/gmock-matchers.hh"

namespace nix {
// Testing of trivial expressions
class TrivialExpressionTest : public LibExprTest
{};

TEST_F(TrivialExpressionTest, true)
{
    auto v = eval("true");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(TrivialExpressionTest, false)
{
    auto v = eval("false");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(TrivialExpressionTest, null)
{
    auto v = eval("null");
    ASSERT_THAT(v, IsNull());
}

TEST_F(TrivialExpressionTest, 1)
{
    auto v = eval("1");
    ASSERT_THAT(v, IsIntEq(1));
}

TEST_F(TrivialExpressionTest, 1plus1)
{
    auto v = eval("1+1");
    ASSERT_THAT(v, IsIntEq(2));
}

TEST_F(TrivialExpressionTest, minus1)
{
    auto v = eval("-1");
    ASSERT_THAT(v, IsIntEq(-1));
}

TEST_F(TrivialExpressionTest, 1minus1)
{
    auto v = eval("1-1");
    ASSERT_THAT(v, IsIntEq(0));
}

TEST_F(TrivialExpressionTest, lambdaAdd)
{
    auto v = eval("let add = a: b: a + b; in add 1 2");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(TrivialExpressionTest, list)
{
    auto v = eval("[]");
    ASSERT_THAT(v, IsListOfSize(0));
}

TEST_F(TrivialExpressionTest, attrs)
{
    auto v = eval("{}");
    ASSERT_THAT(v, IsAttrsOfSize(0));
}

TEST_F(TrivialExpressionTest, float)
{
    auto v = eval("1.234");
    ASSERT_THAT(v, IsFloatEq(1.234));
}

TEST_F(TrivialExpressionTest, updateAttrs)
{
    auto v = eval("{ a = 1; } // { b = 2; a = 3; }");
    ASSERT_THAT(v, IsAttrsOfSize(2));
    auto a = v.attrs()->get(createSymbol("a"));
    ASSERT_NE(a, nullptr);
    ASSERT_THAT(*a->value, IsIntEq(3));

    auto b = v.attrs()->get(createSymbol("b"));
    ASSERT_NE(b, nullptr);
    ASSERT_THAT(*b->value, IsIntEq(2));
}

TEST_F(TrivialExpressionTest, hasAttrOpFalse)
{
    auto v = eval("{} ? a");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(TrivialExpressionTest, hasAttrOpTrue)
{
    auto v = eval("{ a = 123; } ? a");
    ASSERT_THAT(v, IsTrue());
}

TEST_F(TrivialExpressionTest, withFound)
{
    auto v = eval("with { a = 23; }; a");
    ASSERT_THAT(v, IsIntEq(23));
}

TEST_F(TrivialExpressionTest, withNotFound)
{
    ASSERT_THROW(eval("with {}; a"), Error);
}

TEST_F(TrivialExpressionTest, withOverride)
{
    auto v = eval("with { a = 23; }; with { a = 42; }; a");
    ASSERT_THAT(v, IsIntEq(42));
}

TEST_F(TrivialExpressionTest, letOverWith)
{
    auto v = eval("let a = 23; in with { a = 1; }; a");
    ASSERT_THAT(v, IsIntEq(23));
}

TEST_F(TrivialExpressionTest, multipleLet)
{
    auto v = eval("let a = 23; in let a = 42; in a");
    ASSERT_THAT(v, IsIntEq(42));
}

TEST_F(TrivialExpressionTest, defaultFunctionArgs)
{
    auto v = eval("({ a ? 123 }: a) {}");
    ASSERT_THAT(v, IsIntEq(123));
}

TEST_F(TrivialExpressionTest, defaultFunctionArgsOverride)
{
    auto v = eval("({ a ? 123 }: a) { a = 5; }");
    ASSERT_THAT(v, IsIntEq(5));
}

TEST_F(TrivialExpressionTest, defaultFunctionArgsCaptureBack)
{
    auto v = eval("({ a ? 123 }@args: args) {}");
    ASSERT_THAT(v, IsAttrsOfSize(0));
}

TEST_F(TrivialExpressionTest, defaultFunctionArgsCaptureFront)
{
    auto v = eval("(args@{ a ? 123 }: args) {}");
    ASSERT_THAT(v, IsAttrsOfSize(0));
}

TEST_F(TrivialExpressionTest, assertThrows)
{
    ASSERT_THROW(eval("let x = arg: assert arg == 1; 123; in x 2"), Error);
}

TEST_F(TrivialExpressionTest, assertPassed)
{
    auto v = eval("let x = arg: assert arg == 1; 123; in x 1");
    ASSERT_THAT(v, IsIntEq(123));
}

class AttrSetMergeTrvialExpressionTest : public TrivialExpressionTest,
                                         public ::testing::WithParamInterface<const char *>
{};

TEST_P(AttrSetMergeTrvialExpressionTest, attrsetMergeLazy)
{
    // Usually Nix rejects duplicate keys in an attrset but it does allow
    // so if it is an attribute set that contains disjoint sets of keys.
    // The below is equivalent to `{a.b = 1; a.c = 2; }`.
    // The attribute set `a` will be a Thunk at first as the attributes
    // have to be merged (or otherwise computed) and that is done in a lazy
    // manner.

    auto expr = GetParam();
    auto v = eval(expr);
    ASSERT_THAT(v, IsAttrsOfSize(1));

    auto a = v.attrs()->get(createSymbol("a"));
    ASSERT_NE(a, nullptr);

    ASSERT_THAT(*a->value, IsThunk());
    state.forceValue(*a->value, noPos);

    ASSERT_THAT(*a->value, IsAttrsOfSize(2));

    auto b = a->value->attrs()->get(createSymbol("b"));
    ASSERT_NE(b, nullptr);
    ASSERT_THAT(*b->value, IsIntEq(1));

    auto c = a->value->attrs()->get(createSymbol("c"));
    ASSERT_NE(c, nullptr);
    ASSERT_THAT(*c->value, IsIntEq(2));
}

INSTANTIATE_TEST_SUITE_P(
    attrsetMergeLazy,
    AttrSetMergeTrvialExpressionTest,
    ::testing::Values("{ a.b = 1; a.c = 2; }", "{ a = { b = 1; }; a = { c = 2; }; }"));

// The following macros ultimately define 48 tests (16 variations on three
// templates). Each template tests an expression that can be written in 2^4
// different ways, by making four choices about whether to write a particular
// attribute path segment as `x.y = ...;` (collapsed) or `x = { y = ...; };`
// (expanded).
//
// The nestedAttrsetMergeXXXX tests check that the expression
// `{ a.b.c = 1; a.b.d = 2; }` has the same value regardless of how it is
// expanded. (That exact expression is exercised in test
// nestedAttrsetMerge0000, because it is fully collapsed. The test
// nestedAttrsetMerge1001 would instead examine
// `{ a = { b.c = 1; }; a.b = { d = 2; }; }`.)
//
// The nestedAttrsetMergeDupXXXX tests check that the expression
// `{ a.b.c = 1; a.b.c = 2; }` throws a duplicate attribute error, again
// regardless of how it is expanded.
//
// The nestedAttrsetMergeLetXXXX tests check that the expression
// `let a.b.c = 1; a.b.d = 2; in a` has the same value regardless of how it is
// expanded.
#define X_EXPAND_IF0(k, v) k "." v
#define X_EXPAND_IF1(k, v) k " = { " v " };"
#define X4(w, x, y, z)                                                                                                \
    TEST_F(TrivialExpressionTest, nestedAttrsetMerge##w##x##y##z)                                                     \
    {                                                                                                                 \
        auto v = eval(                                                                                                \
            "{ a.b = { c = 1; d = 2; }; } == { " X_EXPAND_IF##w(                                                      \
                "a", X_EXPAND_IF##x("b", "c = 1;")) " " X_EXPAND_IF##y("a", X_EXPAND_IF##z("b", "d = 2;")) " }");     \
        ASSERT_THAT(v, IsTrue());                                                                                     \
    };                                                                                                                \
    TEST_F(TrivialExpressionTest, nestedAttrsetMergeDup##w##x##y##z)                                                  \
    {                                                                                                                 \
        ASSERT_THROW(                                                                                                 \
            eval(                                                                                                     \
                "{ " X_EXPAND_IF##w("a", X_EXPAND_IF##x("b", "c = 1;")) " " X_EXPAND_IF##y(                           \
                    "a", X_EXPAND_IF##z("b", "c = 2;")) " }"),                                                        \
            Error);                                                                                                   \
    };                                                                                                                \
    TEST_F(TrivialExpressionTest, nestedAttrsetMergeLet##w##x##y##z)                                                  \
    {                                                                                                                 \
        auto v = eval(                                                                                                \
            "{ b = { c = 1; d = 2; }; } == (let " X_EXPAND_IF##w(                                                     \
                "a", X_EXPAND_IF##x("b", "c = 1;")) " " X_EXPAND_IF##y("a", X_EXPAND_IF##z("b", "d = 2;")) " in a)"); \
        ASSERT_THAT(v, IsTrue());                                                                                     \
    };
#define X3(...) X4(__VA_ARGS__, 0) X4(__VA_ARGS__, 1)
#define X2(...) X3(__VA_ARGS__, 0) X3(__VA_ARGS__, 1)
#define X1(...) X2(__VA_ARGS__, 0) X2(__VA_ARGS__, 1)
X1(0)
X1(1)
#undef X_EXPAND_IF0
#undef X_EXPAND_IF1
#undef X1
#undef X2
#undef X3
#undef X4

TEST_F(TrivialExpressionTest, functor)
{
    auto v = eval("{ __functor = self: arg: self.v + arg; v = 10; } 5");
    ASSERT_THAT(v, IsIntEq(15));
}

TEST_F(TrivialExpressionTest, forwardPipe)
{
    auto v = eval("1 |> builtins.add 2 |> builtins.mul 3");
    ASSERT_THAT(v, IsIntEq(9));
}

TEST_F(TrivialExpressionTest, backwardPipe)
{
    auto v = eval("builtins.add 1 <| builtins.mul 2 <| 3");
    ASSERT_THAT(v, IsIntEq(7));
}

TEST_F(TrivialExpressionTest, forwardPipeEvaluationOrder)
{
    auto v = eval("1 |> null |> (x: 2)");
    ASSERT_THAT(v, IsIntEq(2));
}

TEST_F(TrivialExpressionTest, backwardPipeEvaluationOrder)
{
    auto v = eval("(x: 1) <| null <| 2");
    ASSERT_THAT(v, IsIntEq(1));
}

TEST_F(TrivialExpressionTest, differentPipeOperatorsDoNotAssociate)
{
    ASSERT_THROW(eval("(x: 1) <| 2 |> (x: 3)"), ParseError);
}

TEST_F(TrivialExpressionTest, differentPipeOperatorsParensLeft)
{
    auto v = eval("((x: 1) <| 2) |> (x: 3)");
    ASSERT_THAT(v, IsIntEq(3));
}

TEST_F(TrivialExpressionTest, differentPipeOperatorsParensRight)
{
    auto v = eval("(x: 1) <| (2 |> (x: 3))");
    ASSERT_THAT(v, IsIntEq(1));
}

TEST_F(TrivialExpressionTest, forwardPipeLowestPrecedence)
{
    auto v = eval("false -> true |> (x: !x)");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(TrivialExpressionTest, backwardPipeLowestPrecedence)
{
    auto v = eval("(x: !x) <| false -> true");
    ASSERT_THAT(v, IsFalse());
}

TEST_F(TrivialExpressionTest, forwardPipeStrongerThanElse)
{
    auto v = eval("if true then 1 else 2 |> 3");
    ASSERT_THAT(v, IsIntEq(1));
}

TEST_F(TrivialExpressionTest, backwardPipeStrongerThanElse)
{
    auto v = eval("if true then 1 else 2 <| 3");
    ASSERT_THAT(v, IsIntEq(1));
}

TEST_F(TrivialExpressionTest, bindOr)
{
    auto v = eval("{ or = 1; }");
    ASSERT_THAT(v, IsAttrsOfSize(1));
    auto b = v.attrs()->get(createSymbol("or"));
    ASSERT_NE(b, nullptr);
    ASSERT_THAT(*b->value, IsIntEq(1));
}

TEST_F(TrivialExpressionTest, orCantBeUsed)
{
    ASSERT_THROW(eval("let or = 1; in or"), Error);
}

TEST_F(TrivialExpressionTest, tooManyFormals)
{
    std::string expr = "let f = { ";
    for (uint32_t i = 0; i <= std::numeric_limits<uint16_t>::max(); ++i) {
        expr += fmt("arg%d, ", i);
    }
    expr += " }: 0 in; f {}";
    ASSERT_THAT(
        [&]() { eval(expr); },
        ::testing::ThrowsMessage<Error>(::nix::testing::HasSubstrIgnoreANSIMatcher(
            "too many formal arguments, implementation supports at most 65535")));
}

} /* namespace nix */
