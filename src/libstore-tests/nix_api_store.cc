#include <fstream>

#include "nix_api_util.h"
#include "nix_api_store.h"

#include "nix/store/tests/nix_api_store.hh"
#include "nix/store/globals.hh"
#include "nix/util/tests/string_callback.hh"
#include "nix/util/url.hh"

#include "store-tests-config.hh"

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
    auto expectedStoreURI = "local://?"
                            + nix::encodeQuery({
                                {"log", nixLogDir},
                                {"state", nixStateDir},
                                {"store", nixStoreDir},
                            });
    ASSERT_EQ(expectedStoreURI, str);
}

TEST_F(nix_api_util_context, nix_store_get_storedir_default)
{
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
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_NIX_ERROR);
}

TEST_F(nix_api_store_test, ReturnsValidStorePath)
{
    StorePath * result = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    ASSERT_NE(result, nullptr);
    ASSERT_STREQ("name", result->path.name().data());
    ASSERT_STREQ(PATH_SUFFIX.substr(1).c_str(), result->path.to_string().data());
    nix_store_path_free(result);
}

TEST_F(nix_api_store_test, SetsLastErrCodeToNixOk)
{
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    ASSERT_EQ(nix_err_code(ctx), NIX_OK);
    nix_store_path_free(path);
}

TEST_F(nix_api_store_test, DoesNotCrashWhenContextIsNull)
{
    StorePath * path = nullptr;
    ASSERT_NO_THROW(path = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str()));
    nix_store_path_free(path);
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
    ASSERT_EQ(NIX_OK, nix_err_code(ctx));
    ASSERT_STREQ("dummy://", store->ptr->config.getReference().render(/*withParams=*/true).c_str());

    std::string str;
    nix_store_get_version(ctx, store, OBSERVE_STRING(str));
    ASSERT_STREQ("", str.c_str());

    nix_store_free(store);
}

TEST_F(nix_api_util_context, nix_store_open_invalid)
{
    nix_libstore_init(ctx);
    Store * store = nix_store_open(ctx, "invalid://", nullptr);
    ASSERT_EQ(NIX_ERR_NIX_ERROR, nix_err_code(ctx));
    ASSERT_EQ(nullptr, store);
    nix_store_free(store);
}

TEST_F(nix_api_store_test, nix_store_is_valid_path_not_in_store)
{
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir + PATH_SUFFIX).c_str());
    ASSERT_EQ(false, nix_store_is_valid_path(ctx, store, path));
    nix_store_path_free(path);
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
    Store * store = nix_store_open(ctx, nix::fmt("file://%s/binary-cache", nix::createTempDir()).c_str(), nullptr);
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

template<typename F>
struct LambdaAdapter
{
    F fun;

    template<typename... Args>
    static inline auto call(LambdaAdapter<F> * ths, Args... args)
    {
        return ths->fun(args...);
    }

    template<typename... Args>
    static auto call_void(void * ths, Args... args)
    {
        return call<Args...>(static_cast<LambdaAdapter<F> *>(ths), args...);
    }

    template<typename... Args>
    static inline auto call2(Args... args, LambdaAdapter<F> * ths)
    {
        return ths->fun(args...);
    }

    template<typename... Args>
    static auto call2_void(Args... args, void * ths)
    {
        return call2<Args...>(args..., static_cast<LambdaAdapter<F> *>(ths));
    }
};

TEST_F(nix_api_store_test_base, build_from_json)
{
    // FIXME get rid of these
    nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");
    nix::settings.substituters = {};

    auto * store = open_local_store();

    StoreDir sd{.store_directory = nixStoreDir.c_str()};

    std::filesystem::path unitTestData{getenv("_NIX_TEST_UNIT_DATA")};

    std::ifstream t{unitTestData / "derivation/ca/self-contained.json"};
    std::stringstream buffer;
    buffer << t.rdbuf();

    auto * drv = nix_derivation_from_json(ctx, sd, buffer.str().c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    auto * drvPath = nix_add_derivation(ctx, store, drv);
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath) {
        auto is_valid_path = nix_store_is_valid_path(ctx, store, outPath);
        ASSERT_EQ(is_valid_path, true);
    }};

    auto ret = nix_store_realise(
        ctx, store, drvPath, static_cast<void *>(&cb), decltype(cb)::call_void<const char *, const StorePath *>);
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // Clean up
    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
    nix_store_free(store);
}

TEST_F(nix_api_store_test_base, build_using_builder)
{
    // FIXME get rid of this
    nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");

    // Get a JSON string for the derivation
    auto drv_json = [&] {
        std::filesystem::path unitTestData{getenv("_NIX_TEST_UNIT_DATA")};

        std::ifstream t{unitTestData / "derivation/ca/self-contained.json"};
        std::stringstream buffer;
        buffer << t.rdbuf();
        return std::move(buffer).str();
    }();

    // Make "store directory" configuration object
    StoreDir sd{.store_directory = nixStoreDir.c_str()};

    // Parse the derivation JSON into an in-memory (opaque to us, users
    // of the C API) derivation.
    auto * drv = nix_derivation_from_json(ctx, sd, drv_json.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    // Create the store dir, because the build will end up placing
    // things here
    nix::createDirs(nixStoreDir);

    // Our derivation has no (pure) inputs in its closure, so this is an
    // empty (null-terminated) array.
    const StorePath * inputPaths[1] = {nullptr};

    // We can use this location for the build
    auto nixBuildDir = nixStateDir + "/build";

    // nonsense path, it doesn't matter! Just used for diagnostics.
    auto * drvPath =
        nix_store_parse_path2(ctx, sd, (nixStoreDir + "/j56sf12rxpcv5swr14vsjn5cwm6bj03h-nyname.drv").c_str());

    // Create the "builder" -- (C API correspondent of the) Nix
    // abstraction for performing builds
    auto * builder = nix_make_derivation_builder(ctx, sd, nixBuildDir.c_str(), drv, drvPath, inputPaths);

    // Start the build
    auto ret = nix_derivation_builder_start(ctx, builder);
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath) {
        auto cb2 = LambdaAdapter{.fun = [&](const char * str, unsigned int) {
            nix::warn("WE JUST MADE THIS %s", str);
            bool ret = exists(std::filesystem::path{str});
            EXPECT_TRUE(ret);
            assert(ret);
        }};

        nix_print_store_path(
            sd,
            outPath,
            decltype(cb2)::call2_void<const char *, unsigned int>,
            static_cast<void *>(&cb2));
    }};

    // Finish the build, i.e. cleanup (TODO expose pipe, so rather than just blocking,
    // the caller do its own event loop before calling this)
    ret = nix_derivation_builder_finish(
        ctx,
        builder,
        static_cast<void *>(&cb),
        decltype(cb)::call_void<const char *, const StorePath *>);
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // TODO register outputs!

    // Clean up
    nix_derivation_builder_free(builder);
    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
}

} // namespace nixC
