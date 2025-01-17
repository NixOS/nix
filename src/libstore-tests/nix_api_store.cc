#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_store.h"
#include "nix_api_store_internal.h"

#include "tests/nix_api_store.hh"
#include "tests/string_callback.hh"

namespace nixC {

std::string PATH_SUFFIX = "/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-name";

TEST_F(nix_api_util_context, nix_libstore_init)
{
    auto ret = nix_libstore_init(ctx);
    ASSERT_EQ(NIX_OK, ret);
}

TEST_F(nix_api_store_test, nix_store_get_uri)
{
    std::string str;
    auto ret = nix_store_get_uri(ctx, store, OBSERVE_STRING(str));
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ("local", str.c_str());
}

TEST_F(nix_api_util_context, nix_store_get_storedir_default)
{
    if (nix::getEnv("HOME").value_or("") == "/homeless-shelter") {
        // skipping test in sandbox because nix_store_open tries to create /nix/var/nix/profiles
        GTEST_SKIP();
    }
    nix_libstore_init(ctx);
    Store * store = nix_store_open(ctx, nullptr, nullptr);
    assert_ctx_ok();
    ASSERT_NE(store, nullptr);

    std::string str;
    auto ret = nix_store_get_storedir(ctx, store, OBSERVE_STRING(str));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);

    // These tests run with a unique storeDir, but not a relocated store
    ASSERT_STREQ(NIX_STORE_DIR, str.c_str());

    nix_store_free(store);
}

TEST_F(nix_api_store_test, nix_store_get_storedir)
{
    std::string str;
    auto ret = nix_store_get_storedir(ctx, store, OBSERVE_STRING(str));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);

    // These tests run with a unique storeDir, but not a relocated store
    ASSERT_STREQ(nixStoreDir.c_str(), str.c_str());
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
    auto ret = nix_store_get_version(ctx, store, OBSERVE_STRING(str));
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
    nix_store_get_version(ctx, store, OBSERVE_STRING(str));
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

TEST_F(nix_api_store_test, nix_store_real_path)
{
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    std::string rp;
    auto ret = nix_store_real_path(ctx, store, path, OBSERVE_STRING(rp));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);
    // Assumption: we're not testing with a relocated store
    ASSERT_STREQ((nixStoreDir + PATH_SUFFIX).c_str(), rp.c_str());

    nix_store_path_free(path);
}

TEST_F(nix_api_util_context, nix_store_real_path_relocated)
{
    if (nix::getEnv("HOME").value_or("") == "/homeless-shelter") {
        // Can't open default store from within sandbox
        GTEST_SKIP();
    }
    auto tmp = nix::createTempDir();
    std::string storeRoot = tmp + "/store";
    std::string stateDir = tmp + "/state";
    std::string logDir = tmp + "/log";
    const char * rootkv[] = {"root", storeRoot.c_str()};
    const char * statekv[] = {"state", stateDir.c_str()};
    const char * logkv[] = {"log", logDir.c_str()};
    // const char * rokv[] = {"read-only", "true"};
    const char ** kvs[] = {rootkv, statekv, logkv, NULL};

    nix_libstore_init(ctx);
    assert_ctx_ok();

    Store * store = nix_store_open(ctx, "local", kvs);
    assert_ctx_ok();
    ASSERT_NE(store, nullptr);

    std::string nixStoreDir;
    auto ret = nix_store_get_storedir(ctx, store, OBSERVE_STRING(nixStoreDir));
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ(NIX_STORE_DIR, nixStoreDir.c_str());

    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    assert_ctx_ok();
    ASSERT_NE(path, nullptr);

    std::string rp;
    ret = nix_store_real_path(ctx, store, path, OBSERVE_STRING(rp));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);

    // Assumption: we're not testing with a relocated store
    ASSERT_STREQ((storeRoot + NIX_STORE_DIR + PATH_SUFFIX).c_str(), rp.c_str());

    nix_store_path_free(path);
}

TEST_F(nix_api_util_context, nix_store_real_path_binary_cache)
{
    if (nix::getEnv("HOME").value_or("") == "/homeless-shelter") {
        // TODO: override NIX_CACHE_HOME?
        // skipping test in sandbox because narinfo cache can't be written
        GTEST_SKIP();
    }

    Store * store = nix_store_open(ctx, "https://cache.nixos.org", nullptr);
    assert_ctx_ok();
    ASSERT_NE(store, nullptr);

    std::string path_raw = std::string(NIX_STORE_DIR) + PATH_SUFFIX;
    StorePath * path = nix_store_parse_path(ctx, store, path_raw.c_str());
    assert_ctx_ok();
    ASSERT_NE(path, nullptr);

    std::string rp;
    auto ret = nix_store_real_path(ctx, store, path, OBSERVE_STRING(rp));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ(path_raw.c_str(), rp.c_str());
}

} // namespace nixC
