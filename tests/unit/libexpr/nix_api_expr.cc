#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"

#include "tests/nix_api_expr.hh"
#include "tests/string_callback.hh"

#include "gmock/gmock.h"
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
    Value * valueFn = nix_alloc_value(nullptr, state);
    nix_expr_eval_from_string(nullptr, stateFn, "builtins.toString", ".", valueFn);
    ASSERT_EQ(NIX_TYPE_FUNCTION, nix_get_type(nullptr, valueFn));

    EvalState * stateResult = nix_state_create(nullptr, nullptr, store);
    Value * valueResult = nix_alloc_value(nullptr, stateResult);
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

    Value * drvPathValue = nix_get_attr_byname(nullptr, value, state, "drvPath");
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

    Value * outPathValue = nix_get_attr_byname(ctx, value, state, "outPath");
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

} // namespace nixC
