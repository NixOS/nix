#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "nix/util/file-system.hh"
#include "nix_api_store.h"
#include "nix_api_util.h"
#include "nix_api_expr.h"
#include "nix_api_value.h"
#include "nix_api_flake.h"
#include "nix_api_eval_cache.h"
#include "nix/util/tests/string_callback.hh"
#include "nix/store/tests/nix_api_store.hh"
#include "nix/util/tests/nix_api_util.hh"
#include "nix_api_fetchers.h"

namespace nixC {

static void collect_name_cb(const char * name, void * user_data)
{
    static_cast<std::vector<std::string> *>(user_data)->emplace_back(name);
}

TEST_F(nix_api_store_test, nix_api_eval_cache_walk)
{
    auto tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, true);

    nix::writeFile(tmpDir / "flake.nix", R"(
        {
            outputs = { ... }: {
                packages.x86_64-linux.hello = derivation {
                    name = "hello";
                    system = "x86_64-linux";
                    builder = "/bin/sh";
                };
            };
        }
    )");

    nix_setting_set(ctx, "experimental-features", "flakes");
    assert_ctx_ok();

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

    nix_flake_settings_add_to_eval_state_builder(ctx, settings, builder);
    assert_ctx_ok();

    nix_eval_state_builder_set_setting(ctx, builder, "eval-cache", "true");
    assert_ctx_ok();
    nix_eval_state_builder_set_setting(ctx, builder, "pure-eval", "true");
    assert_ctx_ok();

    auto state = nix_eval_state_build(ctx, builder);
    assert_ctx_ok();
    ASSERT_NE(nullptr, state);

    nix_eval_state_builder_free(builder);

    auto parseFlags = nix_flake_reference_parse_flags_new(ctx, settings);
    assert_ctx_ok();
    ASSERT_NE(nullptr, parseFlags);

    auto r0 = nix_flake_reference_parse_flags_set_base_directory(
        ctx, parseFlags, tmpDir.string().c_str(), tmpDir.string().size());
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, r0);

    std::string fragment;
    nix_flake_reference * flakeReference = nullptr;
    nix_flake_reference_and_fragment_from_string(
        ctx, fetchSettings, settings, parseFlags, ".", 1, &flakeReference, OBSERVE_STRING(fragment));
    assert_ctx_ok();
    ASSERT_NE(nullptr, flakeReference);

    nix_flake_reference_parse_flags_free(parseFlags);

    auto lockFlags = nix_flake_lock_flags_new(ctx, settings);
    assert_ctx_ok();
    ASSERT_NE(nullptr, lockFlags);

    auto lockedFlake = nix_flake_lock(ctx, fetchSettings, settings, state, lockFlags, flakeReference);
    assert_ctx_ok();
    ASSERT_NE(nullptr, lockedFlake);

    nix_flake_lock_flags_free(lockFlags);

    auto cache = nix_eval_cache_open(ctx, state, lockedFlake);
    assert_ctx_ok();
    ASSERT_NE(nullptr, cache);

    auto root = nix_eval_cache_get_root(ctx, cache);
    assert_ctx_ok();
    ASSERT_NE(nullptr, root);

    auto packages = nix_attr_cursor_maybe_get_attr(ctx, root, "packages");
    assert_ctx_ok();
    ASSERT_NE(nullptr, packages);

    auto system = nix_attr_cursor_maybe_get_attr(ctx, packages, "x86_64-linux");
    assert_ctx_ok();
    ASSERT_NE(nullptr, system);

    std::vector<std::string> names;
    auto rNames = nix_attr_cursor_get_attrs(ctx, state, system, collect_name_cb, &names);
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, rNames);
    ASSERT_NE(std::find(names.begin(), names.end(), "hello"), names.end());

    auto hello = nix_attr_cursor_maybe_get_attr(ctx, system, "hello");
    assert_ctx_ok();
    ASSERT_NE(nullptr, hello);

    bool isDrv = false;
    auto rIsDrv = nix_attr_cursor_is_derivation(ctx, hello, &isDrv);
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, rIsDrv);
    ASSERT_TRUE(isDrv);

    std::string drvPath;
    auto rDrv = nix_attr_cursor_get_drv_path(ctx, state, hello, OBSERVE_STRING(drvPath));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, rDrv);
    ASSERT_FALSE(drvPath.empty());
    ASSERT_TRUE(drvPath.size() >= 4 && drvPath.compare(drvPath.size() - 4, 4, ".drv") == 0);

    auto missing = nix_attr_cursor_maybe_get_attr(ctx, system, "nonexistent");
    assert_ctx_ok();
    ASSERT_EQ(nullptr, missing);

    auto helloValue = nix_attr_cursor_force_value(ctx, state, hello);
    assert_ctx_ok();
    ASSERT_NE(nullptr, helloValue);
    ASSERT_EQ(NIX_TYPE_ATTRS, nix_get_type(ctx, helloValue));
    nix_value_decref(ctx, helloValue);

    ASSERT_EQ(NIX_OK, nix_eval_cache_checkpoint(ctx, cache));
    assert_ctx_ok();

    nix_attr_cursor_free(hello);
    nix_attr_cursor_free(system);
    nix_attr_cursor_free(packages);
    nix_attr_cursor_free(root);
    nix_eval_cache_free(cache);
    nix_locked_flake_free(lockedFlake);
    nix_flake_reference_free(flakeReference);
    nix_state_free(state);
    nix_flake_settings_free(settings);
}

} // namespace nixC
