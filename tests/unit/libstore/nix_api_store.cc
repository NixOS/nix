#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"

#include "tests/nix_api_store.hh"

namespace nixC {

void observe_string_cb(const char * start, unsigned int n, std::string * user_data)
{
    *user_data = std::string(start);
}

std::string PATH_SUFFIX = "/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-name";

TEST_F(nix_api_util_context, nix_libstore_init)
{
    auto ret = nix_libstore_init(ctx);
    ASSERT_EQ(NIX_OK, ret);
}

TEST_F(nix_api_store_test, nix_store_get_uri)
{
    std::string str;
    auto ret = nix_store_get_uri(ctx, store, (void *) observe_string_cb, &str);
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ("local", str.c_str());
}

TEST_F(nix_api_store_test, InvalidPathFails)
{
    nix_store_parse_path(ctx, store, "invalid-path");
    ASSERT_EQ(ctx->last_err_code, NIX_ERR_NIX_ERROR);
}

TEST_F(nix_api_store_test, ReturnsValidStorePath)
{
    StorePath * result = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    ASSERT_NE(result, nullptr);
    ASSERT_STREQ("name", result->path.name().data());
    ASSERT_STREQ(PATH_SUFFIX.substr(1).c_str(), result->path.to_string().data());
}

TEST_F(nix_api_store_test, SetsLastErrCodeToNixOk)
{
    nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    ASSERT_EQ(ctx->last_err_code, NIX_OK);
}

TEST_F(nix_api_store_test, DoesNotCrashWhenContextIsNull)
{
    ASSERT_NO_THROW(nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str()));
}

TEST_F(nix_api_store_test, get_version)
{
    std::string str;
    auto ret = nix_store_get_version(ctx, store, (void *) observe_string_cb, &str);
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ(PACKAGE_VERSION, str.c_str());
}

TEST_F(nix_api_util_context, nix_store_open_dummy)
{
    nix_libstore_init(ctx);
    Store * store = nix_store_open(ctx, "dummy://", nullptr);
    ASSERT_EQ(NIX_OK, ctx->last_err_code);
    ASSERT_STREQ("dummy", store->ptr->getUri().c_str());

    std::string str;
    nix_store_get_version(ctx, store, (void *) observe_string_cb, &str);
    ASSERT_STREQ("", str.c_str());

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
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    ASSERT_EQ(false, nix_store_is_valid_path(ctx, store, path));
}

}
