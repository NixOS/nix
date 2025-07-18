#include "nix/util/config-global.hh"
#include "nix/util/args.hh"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix/util/tests/nix_api_util.hh"
#include "nix/util/tests/string_callback.hh"

#include <gtest/gtest.h>

#include <memory>

#include "util-tests-config.hh"

namespace nixC {

TEST_F(nix_api_util_context, nix_context_error)
{
    std::string err_msg_ref;
    try {
        throw nix::Error("testing error");
    } catch (nix::Error & e) {
        err_msg_ref = e.what();
        nix_context_error(ctx);
    }
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
    ASSERT_EQ(ctx->name, "nix::Error");
    ASSERT_EQ(*ctx->last_err, err_msg_ref);
    ASSERT_EQ(ctx->info->msg.str(), "testing error");

    try {
        throw std::runtime_error("testing exception");
    } catch (std::exception & e) {
        err_msg_ref = e.what();
        nix_context_error(ctx);
    }
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_UNKNOWN);
    ASSERT_EQ(*ctx->last_err, err_msg_ref);

    nix_clear_err(ctx);
    ASSERT_EQ(ctx->last_err_code, NIX_OK);
}

TEST_F(nix_api_util_context, nix_set_err_msg)
{
    ASSERT_EQ(ctx->last_err_code, NIX_OK);
    nix_set_err_msg(ctx, NIX_ERR_UNKNOWN, "unknown test error");
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_UNKNOWN);
    ASSERT_EQ(*ctx->last_err, "unknown test error");
}

TEST(nix_api_util, nix_version_get)
{
    ASSERT_EQ(std::string(nix_version_get()), PACKAGE_VERSION);
}

struct MySettings : nix::Config
{
    nix::Setting<std::string> settingSet{this, "empty", "setting-name", "Description"};
};

MySettings mySettings;
static nix::GlobalConfig::Register rs(&mySettings);

static auto createOwnedNixContext()
{
    return std::unique_ptr<nix_c_context, decltype([](nix_c_context * ctx) {
                               if (ctx)
                                   nix_c_context_free(ctx);
                           })>(nix_c_context_create(), {});
}

TEST_F(nix_api_util_context, nix_setting_get)
{
    ASSERT_EQ(ctx->last_err_code, NIX_OK);
    std::string setting_value;
    nix_err result = nix_setting_get(ctx, "invalid-key", OBSERVE_STRING(setting_value));
    ASSERT_EQ(result, NIX_ERR_KEY);

    result = nix_setting_get(ctx, "setting-name", OBSERVE_STRING(setting_value));
    ASSERT_EQ(result, NIX_OK);
    ASSERT_STREQ("empty", setting_value.c_str());
}

TEST_F(nix_api_util_context, nix_setting_set)
{
    nix_err result = nix_setting_set(ctx, "invalid-key", "new-value");
    ASSERT_EQ(result, NIX_ERR_KEY);

    result = nix_setting_set(ctx, "setting-name", "new-value");
    ASSERT_EQ(result, NIX_OK);

    std::string setting_value;
    result = nix_setting_get(ctx, "setting-name", OBSERVE_STRING(setting_value));
    ASSERT_EQ(result, NIX_OK);
    ASSERT_STREQ("new-value", setting_value.c_str());
}

TEST_F(nix_api_util_context, nix_err_msg)
{
    // no error
    EXPECT_THROW(nix_err_msg(nullptr, ctx, NULL), nix::Error);

    // set error
    nix_set_err_msg(ctx, NIX_ERR_UNKNOWN, "unknown test error");

    // basic usage
    std::string err_msg = nix_err_msg(NULL, ctx, NULL);
    ASSERT_EQ(err_msg, "unknown test error");

    // advanced usage
    unsigned int sz;
    auto new_ctx = createOwnedNixContext();
    err_msg = nix_err_msg(new_ctx.get(), ctx, &sz);
    ASSERT_EQ(sz, err_msg.size());
}

TEST_F(nix_api_util_context, nix_err_info_msg)
{
    std::string err_info;

    // no error
    EXPECT_THROW(nix_err_info_msg(NULL, ctx, OBSERVE_STRING(err_info)), nix::Error);

    try {
        throw nix::Error("testing error");
    } catch (...) {
        nix_context_error(ctx);
    }
    auto new_ctx = createOwnedNixContext();
    nix_err_info_msg(new_ctx.get(), ctx, OBSERVE_STRING(err_info));
    ASSERT_STREQ("testing error", err_info.c_str());
}

TEST_F(nix_api_util_context, nix_err_name)
{
    std::string err_name;

    // no error
    EXPECT_THROW(nix_err_name(NULL, ctx, OBSERVE_STRING(err_name)), nix::Error);

    try {
        throw nix::Error("testing error");
    } catch (...) {
        nix_context_error(ctx);
    }
    auto new_ctx = createOwnedNixContext();
    nix_err_name(new_ctx.get(), ctx, OBSERVE_STRING(err_name));
    ASSERT_EQ(std::string(err_name), "nix::Error");
}

TEST_F(nix_api_util_context, nix_err_code)
{
    ASSERT_EQ(nix_err_code(ctx), NIX_OK);
    nix_set_err_msg(ctx, NIX_ERR_UNKNOWN, "unknown test error");
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_UNKNOWN);
}

} // namespace nixC
