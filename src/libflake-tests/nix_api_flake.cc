#include "nix_api_store.h"
#include "nix_api_store_internal.h"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "nix_api_flake.h"

#include "nix/tests/nix_api_expr.hh"
#include "nix/tests/string_callback.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nixC {

TEST_F(nix_api_store_test, nix_api_init_global_getFlake_exists)
{
    nix_libstore_init(ctx);
    assert_ctx_ok();
    nix_libexpr_init(ctx);
    assert_ctx_ok();

    auto settings = nix_flake_settings_new(ctx);
    assert_ctx_ok();
    ASSERT_NE(nullptr, settings);

    nix_eval_state_builder * builder = nix_eval_state_builder_new(ctx, store);
    ASSERT_NE(nullptr, builder);
    assert_ctx_ok();

    nix_flake_settings_add_to_eval_state_builder(ctx, settings, builder);
    assert_ctx_ok();

    auto state = nix_eval_state_build(ctx, builder);
    assert_ctx_ok();
    ASSERT_NE(nullptr, state);

    nix_eval_state_builder_free(builder);

    auto value = nix_alloc_value(ctx, state);
    assert_ctx_ok();
    ASSERT_NE(nullptr, value);

    nix_err err = nix_expr_eval_from_string(ctx, state, "builtins.getFlake", ".", value);

    nix_state_free(state);

    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, err);
    ASSERT_EQ(NIX_TYPE_FUNCTION, nix_get_type(ctx, value));
}

} // namespace nixC
