#include "nix/util/file-system.hh"
#include "nix_api_store.h"
#include "nix_api_util.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "nix_api_flake.h"

#include "nix/expr/tests/nix_api_expr.hh"
#include "nix/util/tests/string_callback.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace nixC {

TEST_F(nix_api_store_test, nix_api_init_getFlake_exists)
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

TEST_F(nix_api_store_test, nix_api_flake_reference_not_absolute_no_basedir_fail)
{
    nix_libstore_init(ctx);
    assert_ctx_ok();
    nix_libexpr_init(ctx);
    assert_ctx_ok();

    auto settings = nix_flake_settings_new(ctx);
    assert_ctx_ok();
    ASSERT_NE(nullptr, settings);

    auto fetchSettings = nix_fetchers_settings_new(ctx);
    assert_ctx_ok();
    ASSERT_NE(nullptr, fetchSettings);

    auto parseFlags = nix_flake_reference_parse_flags_new(ctx, settings);

    std::string str(".#legacyPackages.aarch127-unknown...orion");
    std::string fragment;
    nix_flake_reference * flakeReference = nullptr;
    auto r = nix_flake_reference_and_fragment_from_string(
        ctx,
        fetchSettings,
        settings,
        parseFlags,
        str.data(),
        str.size(),
        &flakeReference,
        OBSERVE_STRING(fragment));
    
    ASSERT_NE(NIX_OK, r);
    ASSERT_EQ(nullptr, flakeReference);

    nix_flake_reference_parse_flags_free(parseFlags);
}

TEST_F(nix_api_store_test, nix_api_load_flake)
{
    auto tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, true);

    nix::writeFile(tmpDir + "/flake.nix", R"(
        {
            outputs = { ... }: {
                hello = "potato";
            };
        }
    )");

    nix_libstore_init(ctx);
    assert_ctx_ok();
    nix_libexpr_init(ctx);
    assert_ctx_ok();

    auto fetchSettings = nix_fetchers_settings_new(ctx);
    assert_ctx_ok();
    ASSERT_NE(nullptr, fetchSettings);

    auto settings = nix_flake_settings_new(ctx);
    assert_ctx_ok();
    ASSERT_NE(nullptr, settings);

    nix_eval_state_builder * builder = nix_eval_state_builder_new(ctx, store);
    ASSERT_NE(nullptr, builder);
    assert_ctx_ok();

    auto state = nix_eval_state_build(ctx, builder);
    assert_ctx_ok();
    ASSERT_NE(nullptr, state);

    nix_eval_state_builder_free(builder);

    auto parseFlags = nix_flake_reference_parse_flags_new(ctx, settings);
    assert_ctx_ok();
    ASSERT_NE(nullptr, parseFlags);

    auto r0 = nix_flake_reference_parse_flags_set_base_directory(
        ctx,
        parseFlags,
        tmpDir.c_str(),
        tmpDir.size());
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, r0);

    std::string fragment;
    const std::string ref = ".#legacyPackages.aarch127-unknown...orion";
    nix_flake_reference * flakeReference = nullptr;
    auto r = nix_flake_reference_and_fragment_from_string(
        ctx,
        fetchSettings,
        settings,
        parseFlags,
        ref.data(),
        ref.size(),
        &flakeReference,
        OBSERVE_STRING(fragment));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, r);
    ASSERT_NE(nullptr, flakeReference);
    ASSERT_EQ(fragment, "legacyPackages.aarch127-unknown...orion");

    nix_flake_reference_parse_flags_free(parseFlags);

    auto lockFlags = nix_flake_lock_flags_new(ctx, settings);
    assert_ctx_ok();
    ASSERT_NE(nullptr, lockFlags);

    auto lockedFlake = nix_flake_lock(ctx, settings, state, lockFlags, flakeReference);
    assert_ctx_ok();
    ASSERT_NE(nullptr, lockedFlake);

    nix_flake_lock_flags_free(lockFlags);

    auto value = nix_locked_flake_get_output_attrs(ctx, settings, state, lockedFlake);
    assert_ctx_ok();
    ASSERT_NE(nullptr, value);

    auto helloAttr = nix_get_attr_byname(ctx, value, state, "hello");
    assert_ctx_ok();
    ASSERT_NE(nullptr, helloAttr);

    std::string helloStr;
    nix_get_string(ctx, helloAttr, OBSERVE_STRING(helloStr));
    assert_ctx_ok();
    ASSERT_EQ("potato", helloStr);

    nix_value_decref(ctx, value);
    nix_locked_flake_free(lockedFlake);
    nix_flake_reference_free(flakeReference);
    nix_state_free(state);
    nix_flake_settings_free(settings);
}

} // namespace nixC
