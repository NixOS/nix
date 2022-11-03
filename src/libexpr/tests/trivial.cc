#include "libexprtests.hh"

namespace nix {
    // Testing of trivial expressions
    class TrivialExpressionTest : public LibExprTest {};

    TEST_F(TrivialExpressionTest, true) {
        auto v = eval("true");
        ASSERT_THAT(v, IsTrue());
    }

    TEST_F(TrivialExpressionTest, false) {
        auto v = eval("false");
        ASSERT_THAT(v, IsFalse());
    }

    TEST_F(TrivialExpressionTest, null) {
        auto v = eval("null");
        ASSERT_THAT(v, IsNull());
    }

    TEST_F(TrivialExpressionTest, 1) {
        auto v = eval("1");
        ASSERT_THAT(v, IsIntEq(1));
    }

    TEST_F(TrivialExpressionTest, 1plus1) {
        auto v = eval("1+1");
        ASSERT_THAT(v, IsIntEq(2));
    }

    TEST_F(TrivialExpressionTest, minus1) {
        auto v = eval("-1");
        ASSERT_THAT(v, IsIntEq(-1));
    }

    TEST_F(TrivialExpressionTest, 1minus1) {
        auto v = eval("1-1");
        ASSERT_THAT(v, IsIntEq(0));
    }

    TEST_F(TrivialExpressionTest, lambdaAdd) {
        auto v = eval("let add = a: b: a + b; in add 1 2");
        ASSERT_THAT(v, IsIntEq(3));
    }

    TEST_F(TrivialExpressionTest, list) {
        auto v = eval("[]");
        ASSERT_THAT(v, IsListOfSize(0));
    }

    TEST_F(TrivialExpressionTest, attrs) {
        auto v = eval("{}");
        ASSERT_THAT(v, IsAttrsOfSize(0));
    }

    TEST_F(TrivialExpressionTest, float) {
        auto v = eval("1.234");
        ASSERT_THAT(v, IsFloatEq(1.234));
    }

    TEST_F(TrivialExpressionTest, updateAttrs) {
        auto v = eval("{ a = 1; } // { b = 2; a = 3; }");
        ASSERT_THAT(v, IsAttrsOfSize(2));
        auto a = v.attrs->find(createSymbol("a"));
        ASSERT_NE(a, nullptr);
        ASSERT_THAT(*a->value, IsIntEq(3));

        auto b = v.attrs->find(createSymbol("b"));
        ASSERT_NE(b, nullptr);
        ASSERT_THAT(*b->value, IsIntEq(2));
    }

    TEST_F(TrivialExpressionTest, hasAttrOpFalse) {
        auto v = eval("{} ? a");
        ASSERT_THAT(v, IsFalse());
    }

    TEST_F(TrivialExpressionTest, hasAttrOpTrue) {
        auto v = eval("{ a = 123; } ? a");
        ASSERT_THAT(v, IsTrue());
    }

    TEST_F(TrivialExpressionTest, withFound) {
        auto v = eval("with { a = 23; }; a");
        ASSERT_THAT(v, IsIntEq(23));
    }

    TEST_F(TrivialExpressionTest, withNotFound) {
        ASSERT_THROW(eval("with {}; a"), Error);
    }

    TEST_F(TrivialExpressionTest, withOverride) {
        auto v = eval("with { a = 23; }; with { a = 42; }; a");
        ASSERT_THAT(v, IsIntEq(42));
    }

    TEST_F(TrivialExpressionTest, letOverWith) {
        auto v = eval("let a = 23; in with { a = 1; }; a");
        ASSERT_THAT(v, IsIntEq(23));
    }

    TEST_F(TrivialExpressionTest, multipleLet) {
        auto v = eval("let a = 23; in let a = 42; in a");
        ASSERT_THAT(v, IsIntEq(42));
    }

    TEST_F(TrivialExpressionTest, defaultFunctionArgs) {
        auto v = eval("({ a ? 123 }: a) {}");
        ASSERT_THAT(v, IsIntEq(123));
    }

    TEST_F(TrivialExpressionTest, defaultFunctionArgsOverride) {
        auto v = eval("({ a ? 123 }: a) { a = 5; }");
        ASSERT_THAT(v, IsIntEq(5));
    }

    TEST_F(TrivialExpressionTest, defaultFunctionArgsCaptureBack) {
        auto v = eval("({ a ? 123 }@args: args) {}");
        ASSERT_THAT(v, IsAttrsOfSize(0));
    }

    TEST_F(TrivialExpressionTest, defaultFunctionArgsCaptureFront) {
        auto v = eval("(args@{ a ? 123 }: args) {}");
        ASSERT_THAT(v, IsAttrsOfSize(0));
    }

    TEST_F(TrivialExpressionTest, assertThrows) {
        ASSERT_THROW(eval("let x = arg: assert arg == 1; 123; in x 2"), Error);
    }

    TEST_F(TrivialExpressionTest, assertPassed) {
        auto v = eval("let x = arg: assert arg == 1; 123; in x 1");
        ASSERT_THAT(v, IsIntEq(123));
    }

    class AttrSetMergeTrvialExpressionTest :
        public TrivialExpressionTest,
        public testing::WithParamInterface<const char*>
        {};

    TEST_P(AttrSetMergeTrvialExpressionTest, attrsetMergeLazy) {
        // Usually Nix rejects duplicate keys in an attrset but it does allow
        // so if it is an attribute set that contains disjoint sets of keys.
        // The below is equivalent to `{a.b = 1; a.c = 2; }`.
        // The attribute set `a` will be a Thunk at first as the attribuets
        // have to be merged (or otherwise computed) and that is done in a lazy
        // manner.

        auto expr = GetParam();
        auto v = eval(expr);
        ASSERT_THAT(v, IsAttrsOfSize(1));

        auto a = v.attrs->find(createSymbol("a"));
        ASSERT_NE(a, nullptr);

        ASSERT_THAT(*a->value, IsThunk());
        state.forceValue(*a->value, noPos);

        ASSERT_THAT(*a->value, IsAttrsOfSize(2));

        auto b = a->value->attrs->find(createSymbol("b"));
        ASSERT_NE(b, nullptr);
        ASSERT_THAT(*b->value, IsIntEq(1));

        auto c = a->value->attrs->find(createSymbol("c"));
        ASSERT_NE(c, nullptr);
        ASSERT_THAT(*c->value, IsIntEq(2));
    }

    INSTANTIATE_TEST_SUITE_P(
            attrsetMergeLazy,
            AttrSetMergeTrvialExpressionTest,
            testing::Values(
                "{ a.b = 1; a.c = 2; }",
                "{ a = { b = 1; }; a = { c = 2; }; }"
            )
    );

    TEST_F(TrivialExpressionTest, functor) {
        auto v = eval("{ __functor = self: arg: self.v + arg; v = 10; } 5");
        ASSERT_THAT(v, IsIntEq(15));
    }

    TEST_F(TrivialExpressionTest, bindOr) {
        auto v = eval("{ or = 1; }");
        ASSERT_THAT(v, IsAttrsOfSize(1));
        auto b = v.attrs->find(createSymbol("or"));
        ASSERT_NE(b, nullptr);
        ASSERT_THAT(*b->value, IsIntEq(1));
    }

    TEST_F(TrivialExpressionTest, orCantBeUsed) {
        ASSERT_THROW(eval("let or = 1; in or"), Error);
    }
} /* namespace nix */
