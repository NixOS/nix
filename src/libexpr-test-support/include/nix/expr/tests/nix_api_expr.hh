#pragma once
///@file
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "nix/store/tests/nix_api_store.hh"

#include <gtest/gtest.h>

namespace nixC {

class nix_api_expr_test : public nix_api_store_test
{
protected:

    void SetUp() override
    {
        nix_api_store_test::SetUp();
        /* `GTEST_SKIP()` expands to a `return`, so a skip in the base `SetUp()`
           returns from the base only -- control resumes here. Without this guard
           `store` is still null and `nix_state_create` dereferences it, which is
           a segfault rather than a catchable failure. `nix_api_store_test` in
           libstore-tests does not need this because its base has no `SetUp()`
           override; this fixture's does. */
        if (IsSkipped())
            return;
        nix_libexpr_init(ctx);
        state = nix_state_create(nullptr, nullptr, store);
        value = nix_alloc_value(nullptr, state);
    }

    void TearDown() override
    {
        /* Runs even for skipped tests, where these are still null. */
        if (value)
            nix_gc_decref(nullptr, value);
        if (state)
            nix_state_free(state);
    }

    EvalState * state = nullptr;
    nix_value * value = nullptr;
};

} // namespace nixC
