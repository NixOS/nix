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
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_NIX_ERROR);
    ASSERT_EQ(ctx->name, "nix::Error");
    ASSERT_EQ(*ctx->last_err, err_msg_ref);
    ASSERT_EQ(ctx->info->msg.str(), "testing error");

    try {
        throw std::runtime_error("testing exception");
    } catch (std::exception & e) {
        err_msg_ref = e.what();
        nix_context_error(ctx);
    }
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_UNKNOWN);
    ASSERT_EQ(*ctx->last_err, err_msg_ref);

    nix_clear_err(ctx);
    ASSERT_EQ(nix_err_code(ctx), NIX_OK);
}

TEST_F(nix_api_util_context, nix_set_err_msg)
{
    ASSERT_EQ(nix_err_code(ctx), NIX_OK);
    nix_set_err_msg(ctx, NIX_ERR_UNKNOWN, "unknown test error");
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_UNKNOWN);
    ASSERT_EQ(*ctx->last_err, "unknown test error");
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

} // namespace nixC
