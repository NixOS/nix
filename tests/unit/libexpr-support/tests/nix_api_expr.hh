#pragma once
///@file
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "tests/nix_api_store.hh"

#include <gtest/gtest.h>

namespace nixC {

class nix_api_expr_test : public nix_api_store_test
{
protected:

    nix_api_expr_test()
    {
        nix_libexpr_init(ctx);
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

}
