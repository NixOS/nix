#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"

#include "tests/nix_api_expr.hh"
#include "tests/string_callback.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nixC {

TEST_F(nix_api_expr_test, nix_expr_eval_from_string)
{
    nix_expr_eval_from_string(nullptr, state, "builtins.nixVersion", ".", value);
    nix_value_force(nullptr, state, value);
    std::string result;
    nix_get_string(nullptr, value, OBSERVE_STRING(result));

    ASSERT_STREQ(PACKAGE_VERSION, result.c_str());
}

TEST_F(nix_api_expr_test, nix_expr_eval_add_numbers)
{
    nix_expr_eval_from_string(nullptr, state, "1 + 1", ".", value);
    nix_value_force(nullptr, state, value);
    auto result = nix_get_int(nullptr, value);

    ASSERT_EQ(2, result);
}

TEST_F(nix_api_expr_test, nix_expr_eval_drv)
{
    auto expr = R"(derivation { name = "myname"; builder = "mybuilder"; system = "mysystem"; })";
    nix_expr_eval_from_string(nullptr, state, expr, ".", value);
    ASSERT_EQ(NIX_TYPE_ATTRS, nix_get_type(nullptr, value));

    EvalState * stateFn = nix_state_create(nullptr, nullptr, store);
    nix_value * valueFn = nix_alloc_value(nullptr, state);
    nix_expr_eval_from_string(nullptr, stateFn, "builtins.toString", ".", valueFn);
    ASSERT_EQ(NIX_TYPE_FUNCTION, nix_get_type(nullptr, valueFn));

    EvalState * stateResult = nix_state_create(nullptr, nullptr, store);
    nix_value * valueResult = nix_alloc_value(nullptr, stateResult);
    nix_value_call(ctx, stateResult, valueFn, value, valueResult);
    ASSERT_EQ(NIX_TYPE_STRING, nix_get_type(nullptr, valueResult));

    std::string p;
    nix_get_string(nullptr, valueResult, OBSERVE_STRING(p));
    std::string pEnd = "-myname";
    ASSERT_EQ(pEnd, p.substr(p.size() - pEnd.size()));

    // Clean up
    nix_gc_decref(nullptr, valueFn);
    nix_state_free(stateFn);

    nix_gc_decref(nullptr, valueResult);
    nix_state_free(stateResult);
}

TEST_F(nix_api_expr_test, nix_build_drv)
{
    auto expr = R"(derivation { name = "myname";
                                system = builtins.currentSystem;
                                builder = "/bin/sh";
                                args = [ "-c" "echo foo > $out" ];
                              })";
    nix_expr_eval_from_string(nullptr, state, expr, ".", value);

    nix_value * drvPathValue = nix_get_attr_byname(nullptr, value, state, "drvPath");
    std::string drvPath;
    nix_get_string(nullptr, drvPathValue, OBSERVE_STRING(drvPath));

    std::string p = drvPath;
    std::string pEnd = "-myname.drv";
    ASSERT_EQ(pEnd, p.substr(p.size() - pEnd.size()));

    // NOTE: .drvPath should be usually be ignored. Output paths are more versatile.
    //       See https://github.com/NixOS/nix/issues/6507
    //       Use e.g. nix_string_realise to realise the output.
    StorePath * drvStorePath = nix_store_parse_path(ctx, store, drvPath.c_str());
    ASSERT_EQ(true, nix_store_is_valid_path(ctx, store, drvStorePath));

    nix_value * outPathValue = nix_get_attr_byname(ctx, value, state, "outPath");
    std::string outPath;
    nix_get_string(ctx, outPathValue, OBSERVE_STRING(outPath));

    p = outPath;
    pEnd = "-myname";
    ASSERT_EQ(pEnd, p.substr(p.size() - pEnd.size()));
    ASSERT_EQ(true, drvStorePath->path.isDerivation());

    StorePath * outStorePath = nix_store_parse_path(ctx, store, outPath.c_str());
    ASSERT_EQ(false, nix_store_is_valid_path(ctx, store, outStorePath));

    nix_store_realise(ctx, store, drvStorePath, nullptr, nullptr);
    auto is_valid_path = nix_store_is_valid_path(ctx, store, outStorePath);
    ASSERT_EQ(true, is_valid_path);

    // Clean up
    nix_store_path_free(drvStorePath);
    nix_store_path_free(outStorePath);
}

TEST_F(nix_api_expr_test, nix_expr_realise_context_bad_value)
{
    auto expr = "true";
    nix_expr_eval_from_string(ctx, state, expr, ".", value);
    assert_ctx_ok();
    auto r = nix_string_realise(ctx, state, value, false);
    ASSERT_EQ(nullptr, r);
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
    ASSERT_THAT(ctx->last_err, testing::Optional(testing::HasSubstr("cannot coerce")));
}

TEST_F(nix_api_expr_test, nix_expr_realise_context_bad_build)
{
    auto expr = R"(
        derivation { name = "letsbuild";
            system = builtins.currentSystem;
            builder = "/bin/sh";
            args = [ "-c" "echo failing a build for testing purposes; exit 1;" ];
            }
        )";
    nix_expr_eval_from_string(ctx, state, expr, ".", value);
    assert_ctx_ok();
    auto r = nix_string_realise(ctx, state, value, false);
    ASSERT_EQ(nullptr, r);
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
    ASSERT_THAT(ctx->last_err, testing::Optional(testing::HasSubstr("failed with exit code 1")));
}

TEST_F(nix_api_expr_test, nix_expr_realise_context)
{
    // TODO (ca-derivations): add a content-addressed derivation output, which produces a placeholder
    auto expr = R"(
        ''
            a derivation output: ${
                derivation { name = "letsbuild";
                    system = builtins.currentSystem;
                    builder = "/bin/sh";
                    args = [ "-c" "echo foo > $out" ];
                    }}
            a path: ${builtins.toFile "just-a-file" "ooh file good"}
            a derivation path by itself: ${
                builtins.unsafeDiscardOutputDependency
                    (derivation {
                        name = "not-actually-built-yet";
                        system = builtins.currentSystem;
                        builder = "/bin/sh";
                        args = [ "-c" "echo foo > $out" ];
                    }).drvPath}
        ''
        )";
    nix_expr_eval_from_string(ctx, state, expr, ".", value);
    assert_ctx_ok();
    auto r = nix_string_realise(ctx, state, value, false);
    assert_ctx_ok();
    ASSERT_NE(nullptr, r);

    auto s = std::string(nix_realised_string_get_buffer_start(r), nix_realised_string_get_buffer_size(r));

    EXPECT_THAT(s, testing::StartsWith("a derivation output:"));
    EXPECT_THAT(s, testing::HasSubstr("-letsbuild\n"));
    EXPECT_THAT(s, testing::Not(testing::HasSubstr("-letsbuild.drv")));
    EXPECT_THAT(s, testing::HasSubstr("a path:"));
    EXPECT_THAT(s, testing::HasSubstr("-just-a-file"));
    EXPECT_THAT(s, testing::Not(testing::HasSubstr("-just-a-file.drv")));
    EXPECT_THAT(s, testing::Not(testing::HasSubstr("ooh file good")));
    EXPECT_THAT(s, testing::HasSubstr("a derivation path by itself:"));
    EXPECT_THAT(s, testing::EndsWith("-not-actually-built-yet.drv\n"));

    std::vector<std::string> names;
    size_t n = nix_realised_string_get_store_path_count(r);
    for (size_t i = 0; i < n; ++i) {
        const StorePath * p = nix_realised_string_get_store_path(r, i);
        ASSERT_NE(nullptr, p);
        std::string name;
        nix_store_path_name(p, OBSERVE_STRING(name));
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    ASSERT_EQ(3, names.size());
    EXPECT_THAT(names[0], testing::StrEq("just-a-file"));
    EXPECT_THAT(names[1], testing::StrEq("letsbuild"));
    EXPECT_THAT(names[2], testing::StrEq("not-actually-built-yet.drv"));

    nix_realised_string_free(r);
}

const char * SAMPLE_USER_DATA = "whatever";

static void
primop_square(void * user_data, nix_c_context * context, EvalState * state, nix_value ** args, nix_value * ret)
{
    assert(context);
    assert(state);
    assert(user_data == SAMPLE_USER_DATA);
    auto i = nix_get_int(context, args[0]);
    nix_init_int(context, ret, i * i);
}

TEST_F(nix_api_expr_test, nix_expr_primop)
{
    PrimOp * primop =
        nix_alloc_primop(ctx, primop_square, 1, "square", nullptr, "square an integer", (void *) SAMPLE_USER_DATA);
    assert_ctx_ok();
    nix_value * primopValue = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_primop(ctx, primopValue, primop);
    assert_ctx_ok();

    nix_value * three = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_int(ctx, three, 3);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_value_call(ctx, state, primopValue, three, result);
    assert_ctx_ok();

    auto r = nix_get_int(ctx, result);
    ASSERT_EQ(9, r);
}

static void
primop_repeat(void * user_data, nix_c_context * context, EvalState * state, nix_value ** args, nix_value * ret)
{
    assert(context);
    assert(state);
    assert(user_data == SAMPLE_USER_DATA);

    // Get the string to repeat
    std::string s;
    if (nix_get_string(context, args[0], OBSERVE_STRING(s)) != NIX_OK)
        return;

    // Get the number of times to repeat
    auto n = nix_get_int(context, args[1]);
    if (nix_err_code(context) != NIX_OK)
        return;

    // Repeat the string
    std::string result;
    for (int i = 0; i < n; ++i)
        result += s;

    nix_init_string(context, ret, result.c_str());
}

TEST_F(nix_api_expr_test, nix_expr_primop_arity_2_multiple_calls)
{
    PrimOp * primop =
        nix_alloc_primop(ctx, primop_repeat, 2, "repeat", nullptr, "repeat a string", (void *) SAMPLE_USER_DATA);
    assert_ctx_ok();
    nix_value * primopValue = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_primop(ctx, primopValue, primop);
    assert_ctx_ok();

    nix_value * hello = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_string(ctx, hello, "hello");
    assert_ctx_ok();

    nix_value * three = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_int(ctx, three, 3);
    assert_ctx_ok();

    nix_value * partial = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_value_call(ctx, state, primopValue, hello, partial);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_value_call(ctx, state, partial, three, result);
    assert_ctx_ok();

    std::string r;
    nix_get_string(ctx, result, OBSERVE_STRING(r));
    ASSERT_STREQ("hellohellohello", r.c_str());
}

TEST_F(nix_api_expr_test, nix_expr_primop_arity_2_single_call)
{
    PrimOp * primop =
        nix_alloc_primop(ctx, primop_repeat, 2, "repeat", nullptr, "repeat a string", (void *) SAMPLE_USER_DATA);
    assert_ctx_ok();
    nix_value * primopValue = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_primop(ctx, primopValue, primop);
    assert_ctx_ok();

    nix_value * hello = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_string(ctx, hello, "hello");
    assert_ctx_ok();

    nix_value * three = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_int(ctx, three, 3);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    NIX_VALUE_CALL(ctx, state, result, primopValue, hello, three);
    assert_ctx_ok();

    std::string r;
    nix_get_string(ctx, result, OBSERVE_STRING(r));
    assert_ctx_ok();

    ASSERT_STREQ("hellohellohello", r.c_str());
}

static void
primop_bad_no_return(void * user_data, nix_c_context * context, EvalState * state, nix_value ** args, nix_value * ret)
{
}

TEST_F(nix_api_expr_test, nix_expr_primop_bad_no_return)
{
    PrimOp * primop =
        nix_alloc_primop(ctx, primop_bad_no_return, 1, "badNoReturn", nullptr, "a broken primop", nullptr);
    assert_ctx_ok();
    nix_value * primopValue = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_primop(ctx, primopValue, primop);
    assert_ctx_ok();

    nix_value * three = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_int(ctx, three, 3);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_value_call(ctx, state, primopValue, three, result);
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
    ASSERT_THAT(
        ctx->last_err,
        testing::Optional(
            testing::HasSubstr("Implementation error in custom function: return value was not initialized")));
    ASSERT_THAT(ctx->last_err, testing::Optional(testing::HasSubstr("badNoReturn")));
}

static void primop_bad_return_thunk(
    void * user_data, nix_c_context * context, EvalState * state, nix_value ** args, nix_value * ret)
{
    nix_init_apply(context, ret, args[0], args[1]);
}
TEST_F(nix_api_expr_test, nix_expr_primop_bad_return_thunk)
{
    PrimOp * primop =
        nix_alloc_primop(ctx, primop_bad_return_thunk, 2, "badReturnThunk", nullptr, "a broken primop", nullptr);
    assert_ctx_ok();
    nix_value * primopValue = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_primop(ctx, primopValue, primop);
    assert_ctx_ok();

    nix_value * toString = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_expr_eval_from_string(ctx, state, "builtins.toString", ".", toString);
    assert_ctx_ok();

    nix_value * four = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    nix_init_int(ctx, four, 4);
    assert_ctx_ok();

    nix_value * result = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    NIX_VALUE_CALL(ctx, state, result, primopValue, toString, four);

    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
    ASSERT_THAT(
        ctx->last_err,
        testing::Optional(
            testing::HasSubstr("Implementation error in custom function: return value must not be a thunk")));
    ASSERT_THAT(ctx->last_err, testing::Optional(testing::HasSubstr("badReturnThunk")));
}

TEST_F(nix_api_expr_test, nix_value_call_multi_no_args)
{
    nix_value * n = nix_alloc_value(ctx, state);
    nix_init_int(ctx, n, 3);
    assert_ctx_ok();

    nix_value * r = nix_alloc_value(ctx, state);
    nix_value_call_multi(ctx, state, n, 0, nullptr, r);
    assert_ctx_ok();

    auto rInt = nix_get_int(ctx, r);
    assert_ctx_ok();
    ASSERT_EQ(3, rInt);
}
} // namespace nixC
