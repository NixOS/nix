#include "nix_api_store.h"
#include "nix_api_util.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"

#include "nix/expr/tests/nix_api_expr.hh"
#include "nix/util/tests/string_callback.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nixC {

// nix_get_derivation

TEST_F(nix_api_expr_test, nix_get_derivation_returns_drv_path)
{
    auto expr = R"(derivation { name = "myname"; builder = "mybuilder"; system = "mysystem"; })";
    nix_expr_eval_from_string(ctx, state, expr, ".", value);
    assert_ctx_ok();

    StorePath * drvPath = nix_get_derivation(ctx, state, value, false);
    assert_ctx_ok();
    ASSERT_NE(nullptr, drvPath);

    std::string name;
    nix_store_path_name(drvPath, OBSERVE_STRING(name));
    EXPECT_THAT(name, ::testing::HasSubstr("myname"));
    EXPECT_THAT(name, ::testing::EndsWith(".drv"));

    nix_store_path_free(drvPath);
}

TEST_F(nix_api_expr_test, nix_get_derivation_non_derivation_returns_null_without_error)
{
    nix_expr_eval_from_string(ctx, state, "{ a = 1; }", ".", value);
    assert_ctx_ok();

    StorePath * drvPath = nix_get_derivation(ctx, state, value, false);
    // Not a derivation: NULL with no error recorded, so the caller can tell
    // this apart from a genuine failure.
    ASSERT_EQ(nullptr, drvPath);
    ASSERT_EQ(NIX_OK, nix_err_code(ctx));
}

TEST_F(nix_api_expr_test, nix_get_derivation_non_attrset_returns_null_without_error)
{
    nix_expr_eval_from_string(ctx, state, "42", ".", value);
    assert_ctx_ok();

    StorePath * drvPath = nix_get_derivation(ctx, state, value, false);
    ASSERT_EQ(nullptr, drvPath);
    ASSERT_EQ(NIX_OK, nix_err_code(ctx));
}

TEST_F(nix_api_expr_test, nix_get_derivation_null_value_is_error)
{
    StorePath * drvPath = nix_get_derivation(ctx, state, nullptr, false);
    ASSERT_EQ(nullptr, drvPath);
    ASSERT_NE(NIX_OK, nix_err_code(ctx));
}

// A derivation-shaped attribute set whose `name` throws an assertion only when
// forced. The outer set is already WHNF, so nix_get_derivation is what triggers
// the failure (while reading the name), exercising the assertion handling.
static constexpr const char * ASSERTING_DRV = R"({ type = "derivation"; name = assert false; "myname"; })";

TEST_F(nix_api_expr_test, nix_get_derivation_assertion_ignored)
{
    nix_expr_eval_from_string(ctx, state, ASSERTING_DRV, ".", value);
    assert_ctx_ok();

    // With ignoreAssertionFailures = true the assertion is swallowed and the
    // value is reported as "not a derivation": NULL with no error.
    StorePath * drvPath = nix_get_derivation(ctx, state, value, true);
    ASSERT_EQ(nullptr, drvPath);
    ASSERT_EQ(NIX_OK, nix_err_code(ctx));
}

TEST_F(nix_api_expr_test, nix_get_derivation_assertion_propagated)
{
    nix_expr_eval_from_string(ctx, state, ASSERTING_DRV, ".", value);
    assert_ctx_ok();

    // With ignoreAssertionFailures = false the assertion surfaces as an error.
    StorePath * drvPath = nix_get_derivation(ctx, state, value, false);
    ASSERT_EQ(nullptr, drvPath);
    ASSERT_EQ(NIX_ERR_NIX_ERROR, nix_err_code(ctx));
    ASSERT_THAT(nix_err_msg(nullptr, ctx, nullptr), ::testing::HasSubstr("assert"));
}

// nix_value_auto_call_function

TEST_F(nix_api_expr_test, nix_value_auto_call_function_supplies_args)
{
    nix_expr_eval_from_string(ctx, state, "{ a, b }: a + b", ".", value);
    assert_ctx_ok();

    nix_value * args = nix_alloc_value(ctx, state);
    nix_expr_eval_from_string(ctx, state, "{ a = 1; b = 2; }", ".", args);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    nix_value_auto_call_function(ctx, state, args, value, result);
    assert_ctx_ok();

    ASSERT_EQ(3, nix_get_int(ctx, result));

    nix_gc_decref(ctx, args);
    nix_gc_decref(ctx, result);
}

TEST_F(nix_api_expr_test, nix_value_auto_call_function_uses_defaults)
{
    nix_expr_eval_from_string(ctx, state, "{ a, b ? 10 }: a + b", ".", value);
    assert_ctx_ok();

    nix_value * args = nix_alloc_value(ctx, state);
    nix_expr_eval_from_string(ctx, state, "{ a = 5; }", ".", args);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    nix_value_auto_call_function(ctx, state, args, value, result);
    assert_ctx_ok();

    ASSERT_EQ(15, nix_get_int(ctx, result));

    nix_gc_decref(ctx, args);
    nix_gc_decref(ctx, result);
}

TEST_F(nix_api_expr_test, nix_value_auto_call_function_null_args_uses_defaults)
{
    nix_expr_eval_from_string(ctx, state, "{ a ? 7 }: a", ".", value);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    // NULL auto_args supplies no arguments; every formal must then have a
    // default.
    nix_value_auto_call_function(ctx, state, nullptr, value, result);
    assert_ctx_ok();

    ASSERT_EQ(7, nix_get_int(ctx, result));

    nix_gc_decref(ctx, result);
}

TEST_F(nix_api_expr_test, nix_value_auto_call_function_missing_arg_is_error)
{
    nix_expr_eval_from_string(ctx, state, "{ a, b }: a + b", ".", value);
    assert_ctx_ok();

    nix_value * args = nix_alloc_value(ctx, state);
    nix_expr_eval_from_string(ctx, state, "{ a = 1; }", ".", args);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    // 'b' has neither a supplied value nor a default. The argument name in the
    // message is colorized, so use the ANSI-stripping matcher to assert on it.
    nix_value_auto_call_function(ctx, state, args, value, result);
    ASSERT_EQ(NIX_ERR_NIX_ERROR, nix_err_code(ctx));
    ASSERT_THAT(
        nix_err_msg(nullptr, ctx, nullptr),
        ::nix::testing::HasSubstrIgnoreANSIMatcher(
            "cannot evaluate a function that has an argument without a value ('b')"));

    nix_gc_decref(ctx, args);
    nix_gc_decref(ctx, result);
}

TEST_F(nix_api_expr_test, nix_value_auto_call_function_non_function_passthrough)
{
    nix_expr_eval_from_string(ctx, state, "42", ".", value);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    // A non-function value is returned unchanged.
    nix_value_auto_call_function(ctx, state, nullptr, value, result);
    assert_ctx_ok();

    ASSERT_EQ(42, nix_get_int(ctx, result));

    nix_gc_decref(ctx, result);
}

TEST_F(nix_api_expr_test, nix_value_auto_call_function_single_arg_lambda_passthrough)
{
    nix_expr_eval_from_string(ctx, state, "x: x + 1", ".", value);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    // A function taking a single unnamed argument has no named arguments to
    // fill, so it is returned unchanged rather than being called.
    nix_value_auto_call_function(ctx, state, nullptr, value, result);
    assert_ctx_ok();

    ASSERT_EQ(NIX_TYPE_FUNCTION, nix_get_type(ctx, result));

    nix_gc_decref(ctx, result);
}

TEST_F(nix_api_expr_test, nix_value_auto_call_function_null_fn_is_error)
{
    nix_value * result = nix_alloc_value(ctx, state);
    nix_value_auto_call_function(ctx, state, nullptr, nullptr, result);
    ASSERT_NE(NIX_OK, nix_err_code(ctx));

    nix_gc_decref(ctx, result);
}

} // namespace nixC
