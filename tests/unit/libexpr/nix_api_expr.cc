#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"

#include "tests/nix_api_expr.hh"

#include <gtest/gtest.h>

namespace nixC {

TEST_F(nix_api_expr_test, nix_expr_eval_from_string)
{
    nix_expr_eval_from_string(nullptr, state, "builtins.nixVersion", ".", value);
    nix_value_force(nullptr, state, value);
    auto result = nix_get_string(nullptr, value);

    ASSERT_STREQ(PACKAGE_VERSION, result);
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

    std::string p = nix_get_string(nullptr, valueResult);
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
    const char * drvPath = nix_get_string(nullptr, drvPathValue);

    std::string p = drvPath;
    std::string pEnd = "-myname.drv";
    ASSERT_EQ(pEnd, p.substr(p.size() - pEnd.size()));

    StorePath * drvStorePath = nix_store_parse_path(ctx, store, drvPath);
    ASSERT_EQ(true, nix_store_is_valid_path(ctx, store, drvStorePath));

    Value * outPathValue = nix_get_attr_byname(ctx, value, state, "outPath");
    const char * outPath = nix_get_string(ctx, outPathValue);

    p = outPath;
    pEnd = "-myname";
    ASSERT_EQ(pEnd, p.substr(p.size() - pEnd.size()));
    ASSERT_EQ(true, drvStorePath->path.isDerivation());

    StorePath * outStorePath = nix_store_parse_path(ctx, store, outPath);
    ASSERT_EQ(false, nix_store_is_valid_path(ctx, store, outStorePath));

    // TODO figure out why fails.
    // `make libexpr-tests_RUN` works, but `nix build .` enters an infinite loop
    /* nix_store_realise(ctx, store, drvStorePath, nullptr, nullptr); */
    /* auto is_valid_path = nix_store_is_valid_path(ctx, store, outStorePath); */
    /* ASSERT_EQ(true, is_valid_path); */

    // Clean up
    nix_store_path_free(drvStorePath);
    nix_store_path_free(outStorePath);
}
}
