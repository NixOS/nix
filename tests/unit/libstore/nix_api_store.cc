#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"

#include "tests/nix_api_store.hh"

#define STORE_DIR "/nix/store/"
#define HASH_PART "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q"
const char * validPath = STORE_DIR HASH_PART "-x";

namespace nixC {

TEST_F(nix_api_util_context, nix_libstore_init)
{
    auto ret = nix_libstore_init(ctx);
    ASSERT_EQ(NIX_OK, ret);
}

TEST_F(nix_api_store_test, nix_store_get_uri)
{
    char value[256];
    auto ret = nix_store_get_uri(ctx, store, value, 256);
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ("local", value);
}

TEST_F(nix_api_store_test, InvalidPathFails)
{
    nix_store_parse_path(ctx, store, "invalid-path");
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
}

TEST_F(nix_api_store_test, ReturnsValidStorePath)
{
    StorePath * result = nix_store_parse_path(ctx, store, validPath);
    ASSERT_NE(result, nullptr);
    ASSERT_STREQ("x", result->path.name().data());
    ASSERT_STREQ(HASH_PART "-x", result->path.to_string().data());
}

TEST_F(nix_api_store_test, SetsLastErrCodeToNixOk)
{
    nix_store_parse_path(ctx, store, validPath);
    ASSERT_EQ(ctx->last_err_code, NIX_OK);
}

TEST_F(nix_api_store_test, DoesNotCrashWhenContextIsNull)
{
    ASSERT_NO_THROW(nix_store_parse_path(nullptr, store, validPath));
}

TEST_F(nix_api_store_test, get_version)
{
    char value[256];
    auto ret = nix_store_get_version(ctx, store, value, 256);
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ(PACKAGE_VERSION, value);
}

TEST_F(nix_api_util_context, nix_store_open_dummy)
{
    nix_libstore_init(ctx);
    Store * store = nix_store_open(ctx, "dummy://", nullptr);
    ASSERT_EQ(NIX_OK, ctx->last_err_code);
    ASSERT_STREQ("dummy", store->ptr->getUri().c_str());

    char value[256];
    nix_store_get_version(ctx, store, value, 256);
    ASSERT_STREQ("", value);

    nix_store_free(store);
}

TEST_F(nix_api_util_context, nix_store_open_invalid)
{
    nix_libstore_init(ctx);
    Store * store = nix_store_open(ctx, "invalid://", nullptr);
    ASSERT_EQ(NIX_ERR_NIX_ERROR, ctx->last_err_code);
    ASSERT_EQ(nullptr, store);
    nix_store_free(store);
}

TEST_F(nix_api_store_test, nix_store_is_valid_path_not_in_store)
{
    StorePath * path = nix_store_parse_path(ctx, store, validPath);
    ASSERT_EQ(false, nix_store_is_valid_path(ctx, store, path));
}

}
