#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"

#include "tests/nix_api_store.hh"

#include <gtest/gtest.h>

namespace nixC {

class nix_api_expr_test : public nix_api_store_test
{
public:
    nix_api_expr_test()
    {
        state = nix_state_create(nullptr, nullptr, store);
        value = nix_alloc_value(nullptr, state);
    }
    ~nix_api_expr_test()
    {
        nix_gc_decref(nullptr, value);
        nix_state_free(state);
    }

    EvalState * state;
    Value * value;
};

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
    nix_value_force(nullptr, state, value);

    ASSERT_EQ(NIX_TYPE_ATTRS, nix_get_type(nullptr, value));

    auto stateFn = nix_state_create(nullptr, nullptr, store);
    auto valueFn = nix_alloc_value(nullptr, state);
    nix_expr_eval_from_string(nullptr, stateFn, "builtins.toString", ".", valueFn);

    ASSERT_EQ(NIX_TYPE_FUNCTION, nix_get_type(nullptr, valueFn));

    auto stateResult = nix_state_create(nullptr, nullptr, store);
    auto valueResult = nix_alloc_value(nullptr, stateResult);

    nix_value_call(ctx, stateResult, valueFn, value, valueResult);
    nix_value_force(nullptr, stateResult, valueResult);

    auto p = nix_get_string(nullptr, valueResult);

    ASSERT_STREQ("/nix/store/40s0qmrfb45vlh6610rk29ym318dswdr-myname", p);

    // Clean up
    nix_gc_decref(nullptr, valueFn);
    nix_state_free(stateFn);

    nix_gc_decref(nullptr, valueResult);
    nix_state_free(stateResult);
}

}
