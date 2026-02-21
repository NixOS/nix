#include <fstream>

#include <nlohmann/json.hpp>

#include "nix_api_util.h"
#include "nix_api_store.h"

#include "nix/store/tests/nix_api_store.hh"
#include "nix/store/globals.hh"
#include "nix/util/tests/string_callback.hh"
#include "nix/util/tests/test-data.hh"
#include "nix/util/url.hh"

#include "store-tests-config.hh"

namespace nixC {

std::string PATH_SUFFIX = "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-name";

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
                                {"log", nixLogDir.string()},
                                {"state", nixStateDir.string()},
                                {"store", nixStoreDir.string()},
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

#ifdef _WIN32
    // On Windows, the default store is under ProgramData
    ASSERT_TRUE(str.ends_with("\\nix\\store") || str.ends_with("/nix/store"));
#else
    ASSERT_STREQ(NIX_STORE_DIR, str.c_str());
#endif

    nix_store_free(store);
}

TEST_F(nix_api_store_test, nix_store_get_storedir)
{
    std::string str;
    auto ret = nix_store_get_storedir(ctx, store, OBSERVE_STRING(str));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);

    // These tests run with a unique storeDir, but not a relocated store
    ASSERT_STREQ(nixStoreDir.string().c_str(), str.c_str());
}

TEST_F(nix_api_store_test, InvalidPathFails)
{
    nix_store_parse_path(ctx, store, "invalid-path");
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_NIX_ERROR);
}

TEST_F(nix_api_store_test, ReturnsValidStorePath)
{
    StorePath * result = nix_store_parse_path(ctx, store, (nixStoreDir / PATH_SUFFIX).string().c_str());
    ASSERT_NE(result, nullptr);
    ASSERT_STREQ("name", result->path.name().data());
    ASSERT_STREQ(PATH_SUFFIX.c_str(), result->path.to_string().data());
    nix_store_path_free(result);
}

TEST_F(nix_api_store_test, SetsLastErrCodeToNixOk)
{
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir / PATH_SUFFIX).string().c_str());
    ASSERT_EQ(nix_err_code(ctx), NIX_OK);
    nix_store_path_free(path);
}

TEST_F(nix_api_store_test, DoesNotCrashWhenContextIsNull)
{
    StorePath * path = nullptr;
    ASSERT_NO_THROW(path = nix_store_parse_path(ctx, store, (nixStoreDir / PATH_SUFFIX).string().c_str()));
    nix_store_path_free(path);
}

// Verify it's 20 bytes
static_assert(sizeof(nix_store_path_hash_part::bytes) == 20);
static_assert(sizeof(nix_store_path_hash_part::bytes) == sizeof(nix_store_path_hash_part));

TEST_F(nix_api_store_test, nix_store_path_hash)
{
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir / PATH_SUFFIX).string().c_str());
    ASSERT_NE(path, nullptr);

    nix_store_path_hash_part hash;
    auto ret = nix_store_path_hash(ctx, path, &hash);
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // The hash should be non-zero
    bool allZero = true;
    for (size_t i = 0; i < sizeof(hash.bytes); i++) {
        if (hash.bytes[i] != 0) {
            allZero = false;
            break;
        }
    }
    ASSERT_FALSE(allZero);

    nix_store_path_free(path);
}

TEST_F(nix_api_store_test, nix_store_create_from_parts_roundtrip)
{
    // Parse a path
    StorePath * original = nix_store_parse_path(ctx, store, (nixStoreDir / PATH_SUFFIX).string().c_str());
    EXPECT_NE(original, nullptr);

    // Get its hash
    nix_store_path_hash_part hash;
    auto ret = nix_store_path_hash(ctx, original, &hash);
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // Get its name
    std::string name;
    nix_store_path_name(original, OBSERVE_STRING(name));

    // Reconstruct from parts
    StorePath * reconstructed = nix_store_create_from_parts(ctx, &hash, name.c_str(), name.size());
    assert_ctx_ok();
    ASSERT_NE(reconstructed, nullptr);

    // Should be equal
    EXPECT_EQ(original->path, reconstructed->path);

    nix_store_path_free(original);
    nix_store_path_free(reconstructed);
}

TEST_F(nix_api_store_test, nix_store_create_from_parts_invalid_name)
{
    nix_store_path_hash_part hash = {};
    // Invalid name with spaces
    StorePath * path = nix_store_create_from_parts(ctx, &hash, "invalid name", 12);
    ASSERT_EQ(path, nullptr);
    ASSERT_EQ(nix_err_code(ctx), NIX_ERR_NIX_ERROR);
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
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir / PATH_SUFFIX).string().c_str());
    ASSERT_EQ(false, nix_store_is_valid_path(ctx, store, path));
    nix_store_path_free(path);
}

TEST_F(nix_api_store_test, nix_store_real_path)
{
    StorePath * path = nix_store_parse_path(ctx, store, (nixStoreDir / PATH_SUFFIX).string().c_str());
    std::string rp;
    auto ret = nix_store_real_path(ctx, store, path, OBSERVE_STRING(rp));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);
    // Assumption: we're not testing with a relocated store
    ASSERT_STREQ((nixStoreDir / PATH_SUFFIX).string().c_str(), rp.c_str());

    nix_store_path_free(path);
}

TEST_F(nix_api_util_context, nix_store_real_path_relocated)
{
    auto tmp = nix::createTempDir();
    auto stateDir = (tmp / "state").string();
    auto logDir = (tmp / "log").string();
#ifdef _WIN32
    // Don't depend on known folders which could change on windows.
    // On Windows we can't combine two absolute paths, so we need to
    // explicitly set the real store dir.
    std::filesystem::path logicalStoreDir = "X:\\nix\\store";
    auto realStoreDir = tmp / "store" / "X" / "nix" / "store";
#else
    std::filesystem::path logicalStoreDir = NIX_STORE_DIR;
    auto realStoreDir = tmp / "store" / logicalStoreDir.relative_path();
#endif
    auto logicalStoreDirStr = logicalStoreDir.string();
    auto realStoreDirStr = realStoreDir.string();
    const char * realStorekv[] = {"real", realStoreDirStr.c_str()};
    const char * statekv[] = {"state", stateDir.c_str()};
    const char * logkv[] = {"log", logDir.c_str()};
    const char * storekv[] = {"store", logicalStoreDirStr.c_str()};
    // const char * rokv[] = {"read-only", "true"};
    const char ** kvs[] = {realStorekv, statekv, logkv, storekv, NULL};

    nix_libstore_init(ctx);
    assert_ctx_ok();

    Store * store = nix_store_open(ctx, "local", kvs);
    assert_ctx_ok();
    ASSERT_NE(store, nullptr);

    std::string nixStoreDir;
    auto ret = nix_store_get_storedir(ctx, store, OBSERVE_STRING(nixStoreDir));
    ASSERT_EQ(NIX_OK, ret);
    ASSERT_STREQ(logicalStoreDirStr.c_str(), nixStoreDir.c_str());

    StorePath * path =
        nix_store_parse_path(ctx, store, (std::filesystem::path{nixStoreDir} / PATH_SUFFIX).string().c_str());
    assert_ctx_ok();
    ASSERT_NE(path, nullptr);

    std::string rp;
    ret = nix_store_real_path(ctx, store, path, OBSERVE_STRING(rp));
    assert_ctx_ok();
    ASSERT_EQ(NIX_OK, ret);

    auto expectedPath = realStoreDir / PATH_SUFFIX;
    ASSERT_EQ(expectedPath, std::filesystem::path{rp});

    nix_store_path_free(path);
}

TEST_F(nix_api_util_context, nix_store_real_path_binary_cache)
{
    auto tempPath = nix::pathToUrlPath(nix::createTempDir() / "binary-cache");
    Store * store = nix_store_open(ctx, ("file://" + nix::encodeUrlPath(tempPath)).c_str(), nullptr);
    assert_ctx_ok();
    ASSERT_NE(store, nullptr);

    std::string nixStoreDir;
    {
        auto ret = nix_store_get_storedir(ctx, store, OBSERVE_STRING(nixStoreDir));
        ASSERT_EQ(NIX_OK, ret);
    }

    std::string path_raw = nixStoreDir + "/" + PATH_SUFFIX;
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
        return call(static_cast<LambdaAdapter<F> *>(ths), args...);
    }
};

class NixApiStoreTestWithRealisedPath : public nix_api_store_test_base
{
public:
    StorePath * drvPath = nullptr;
    nix_derivation * drv = nullptr;
    Store * store = nullptr;
    StorePath * outPath = nullptr;

    void SetUp() override
    {
        nix_api_store_test_base::SetUp();

        nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");
        nix::settings.getWorkerSettings().substituters = {};

        store = open_local_store();

        std::filesystem::path unitTestData = nix::getUnitTestData();
        std::ifstream t{unitTestData / "derivation/ca/self-contained.json"};
        std::stringstream buffer;
        buffer << t.rdbuf();

        // Replace the hardcoded system with the current system
        std::string jsonStr = nix::replaceStrings(buffer.str(), "x86_64-linux", nix::settings.thisSystem.get());

        drv = nix_derivation_from_json(ctx, store, jsonStr.c_str());
        assert_ctx_ok();
        ASSERT_NE(drv, nullptr);

        drvPath = nix_add_derivation(ctx, store, drv);
        assert_ctx_ok();
        ASSERT_NE(drvPath, nullptr);

        auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath_) {
            ASSERT_NE(outname, nullptr) << "Output name should not be NULL";
            auto is_valid_path = nix_store_is_valid_path(ctx, store, outPath_);
            ASSERT_EQ(is_valid_path, true);
            ASSERT_STREQ(outname, "out") << "Expected single 'out' output";
            ASSERT_EQ(outPath, nullptr) << "Output path callback should only be called once";
            outPath = nix_store_path_clone(outPath_);
        }};

        auto ret = nix_store_realise(
            ctx, store, drvPath, static_cast<void *>(&cb), decltype(cb)::call_void<const char *, const StorePath *>);
        assert_ctx_ok();
        ASSERT_EQ(ret, NIX_OK);
        ASSERT_NE(outPath, nullptr) << "Derivation should have produced an output";
    }

    void TearDown() override
    {
        if (drvPath)
            nix_store_path_free(drvPath);
        if (outPath)
            nix_store_path_free(outPath);
        if (drv)
            nix_derivation_free(drv);
        if (store)
            nix_store_free(store);

        nix_api_store_test_base::TearDown();
    }
};

TEST_F(nix_api_store_test_base, build_from_json)
{
    // FIXME get rid of these
    nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");
    nix::settings.getWorkerSettings().substituters = {};

    auto * store = open_local_store();

    std::filesystem::path unitTestData = nix::getUnitTestData();

    std::ifstream t{unitTestData / "derivation/ca/self-contained.json"};
    std::stringstream buffer;
    buffer << t.rdbuf();

    // Replace the hardcoded system with the current system
    std::string jsonStr = nix::replaceStrings(buffer.str(), "x86_64-linux", nix::settings.thisSystem.get());

    auto * drv = nix_derivation_from_json(ctx, store, jsonStr.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    auto * drvPath = nix_add_derivation(ctx, store, drv);
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    int callbackCount = 0;
    auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath) {
        ASSERT_NE(outname, nullptr);
        ASSERT_STREQ(outname, "out");
        ASSERT_NE(outPath, nullptr);
        auto is_valid_path = nix_store_is_valid_path(ctx, store, outPath);
        ASSERT_EQ(is_valid_path, true);
        callbackCount++;
    }};

    auto ret = nix_store_realise(
        ctx, store, drvPath, static_cast<void *>(&cb), decltype(cb)::call_void<const char *, const StorePath *>);
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);
    ASSERT_EQ(callbackCount, 1) << "Callback should have been invoked exactly once";

    // Clean up
    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
    nix_store_free(store);
}

TEST_F(nix_api_store_test_base, nix_store_realise_invalid_system)
{
    // Test that nix_store_realise properly reports errors when the system is invalid
    nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");
    nix::settings.getWorkerSettings().substituters = {};

    auto * store = open_local_store();

    std::filesystem::path unitTestData = nix::getUnitTestData();
    std::ifstream t{unitTestData / "derivation/ca/self-contained.json"};
    std::stringstream buffer;
    buffer << t.rdbuf();

    // Use an invalid system that cannot be built
    std::string jsonStr = nix::replaceStrings(buffer.str(), "x86_64-linux", "bogus65-bogusos");

    auto * drv = nix_derivation_from_json(ctx, store, jsonStr.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    auto * drvPath = nix_add_derivation(ctx, store, drv);
    assert_ctx_ok();
    ASSERT_NE(drvPath, nullptr);

    int callbackCount = 0;
    auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath) { callbackCount++; }};

    auto ret = nix_store_realise(
        ctx, store, drvPath, static_cast<void *>(&cb), decltype(cb)::call_void<const char *, const StorePath *>);

    // Should fail with an error
    ASSERT_NE(ret, NIX_OK);
    ASSERT_EQ(callbackCount, 0) << "Callback should not be invoked when build fails";

    // Check that error message is set
    std::string errMsg = nix_err_msg(nullptr, ctx, nullptr);
    ASSERT_FALSE(errMsg.empty()) << "Error message should be set";
    ASSERT_NE(errMsg.find("system"), std::string::npos) << "Error should mention system";

    // Clean up
    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
    nix_store_free(store);
}

TEST_F(nix_api_store_test_base, nix_store_realise_builder_fails)
{
    // Test that nix_store_realise properly reports errors when the builder fails
    nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");
    nix::settings.getWorkerSettings().substituters = {};

    auto * store = open_local_store();

    std::filesystem::path unitTestData = nix::getUnitTestData();
    std::ifstream t{unitTestData / "derivation/ca/self-contained.json"};
    std::stringstream buffer;
    buffer << t.rdbuf();

    // Replace with current system and make builder command fail
    std::string jsonStr = nix::replaceStrings(buffer.str(), "x86_64-linux", nix::settings.thisSystem.get());
    jsonStr = nix::replaceStrings(jsonStr, "echo $name foo > $out", "exit 1");

    auto * drv = nix_derivation_from_json(ctx, store, jsonStr.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    auto * drvPath = nix_add_derivation(ctx, store, drv);
    assert_ctx_ok();
    ASSERT_NE(drvPath, nullptr);

    int callbackCount = 0;
    auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath) { callbackCount++; }};

    auto ret = nix_store_realise(
        ctx, store, drvPath, static_cast<void *>(&cb), decltype(cb)::call_void<const char *, const StorePath *>);

    // Should fail with an error
    ASSERT_NE(ret, NIX_OK);
    ASSERT_EQ(callbackCount, 0) << "Callback should not be invoked when build fails";

    // Check that error message is set
    std::string errMsg = nix_err_msg(nullptr, ctx, nullptr);
    ASSERT_FALSE(errMsg.empty()) << "Error message should be set";

    // Clean up
    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
    nix_store_free(store);
}

TEST_F(nix_api_store_test_base, nix_store_realise_builder_no_output)
{
    // Test that nix_store_realise properly reports errors when builder succeeds but produces no output
    nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");
    nix::settings.getWorkerSettings().substituters = {};

    auto * store = open_local_store();

    std::filesystem::path unitTestData = nix::getUnitTestData();
    std::ifstream t{unitTestData / "derivation/ca/self-contained.json"};
    std::stringstream buffer;
    buffer << t.rdbuf();

    // Replace with current system and make builder succeed but not produce output
    std::string jsonStr = nix::replaceStrings(buffer.str(), "x86_64-linux", nix::settings.thisSystem.get());
    jsonStr = nix::replaceStrings(jsonStr, "echo $name foo > $out", "true");

    auto * drv = nix_derivation_from_json(ctx, store, jsonStr.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    auto * drvPath = nix_add_derivation(ctx, store, drv);
    assert_ctx_ok();
    ASSERT_NE(drvPath, nullptr);

    int callbackCount = 0;
    auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath) { callbackCount++; }};

    auto ret = nix_store_realise(
        ctx, store, drvPath, static_cast<void *>(&cb), decltype(cb)::call_void<const char *, const StorePath *>);

    // Should fail with an error
    ASSERT_NE(ret, NIX_OK);
    ASSERT_EQ(callbackCount, 0) << "Callback should not be invoked when build produces no output";

    // Check that error message is set
    std::string errMsg = nix_err_msg(nullptr, ctx, nullptr);
    ASSERT_FALSE(errMsg.empty()) << "Error message should be set";

    // Clean up
    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
    nix_store_free(store);
}

TEST_F(NixApiStoreTestWithRealisedPath, nix_store_get_fs_closure_with_outputs)
{
    // Test closure computation with include_outputs on a derivation path
    struct CallbackData
    {
        std::set<std::string> * paths;
    };

    std::set<std::string> closure_paths;
    CallbackData data{&closure_paths};

    auto ret = nix_store_get_fs_closure(
        ctx,
        store,
        drvPath, // Use derivation path
        false,   // flip_direction
        true,    // include_outputs - include the outputs in the closure
        false,   // include_derivers
        &data,
        [](nix_c_context * context, void * userdata, const StorePath * path) {
            auto * data = static_cast<CallbackData *>(userdata);
            std::string path_str;
            nix_store_path_name(path, OBSERVE_STRING(path_str));
            auto [it, inserted] = data->paths->insert(path_str);
            ASSERT_TRUE(inserted) << "Duplicate path in closure: " << path_str;
        });
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // The closure should contain the derivation and its outputs
    ASSERT_GE(closure_paths.size(), 2);

    // Verify the output path is in the closure
    std::string outPathName;
    nix_store_path_name(outPath, OBSERVE_STRING(outPathName));
    ASSERT_EQ(closure_paths.count(outPathName), 1);
}

TEST_F(NixApiStoreTestWithRealisedPath, nix_store_get_fs_closure_without_outputs)
{
    // Test closure computation WITHOUT include_outputs on a derivation path
    struct CallbackData
    {
        std::set<std::string> * paths;
    };

    std::set<std::string> closure_paths;
    CallbackData data{&closure_paths};

    auto ret = nix_store_get_fs_closure(
        ctx,
        store,
        drvPath, // Use derivation path
        false,   // flip_direction
        false,   // include_outputs - do NOT include the outputs
        false,   // include_derivers
        &data,
        [](nix_c_context * context, void * userdata, const StorePath * path) {
            auto * data = static_cast<CallbackData *>(userdata);
            std::string path_str;
            nix_store_path_name(path, OBSERVE_STRING(path_str));
            auto [it, inserted] = data->paths->insert(path_str);
            ASSERT_TRUE(inserted) << "Duplicate path in closure: " << path_str;
        });
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // Verify the output path is NOT in the closure
    std::string outPathName;
    nix_store_path_name(outPath, OBSERVE_STRING(outPathName));
    ASSERT_EQ(closure_paths.count(outPathName), 0) << "Output path should not be in closure when includeOutputs=false";
}

TEST_F(NixApiStoreTestWithRealisedPath, nix_store_get_fs_closure_flip_direction)
{
    // Test closure computation with flip_direction on a derivation path
    // When flip_direction=true, we get the reverse dependencies (what depends on this path)
    // For a derivation, this should NOT include outputs even with include_outputs=true
    struct CallbackData
    {
        std::set<std::string> * paths;
    };

    std::set<std::string> closure_paths;
    CallbackData data{&closure_paths};

    auto ret = nix_store_get_fs_closure(
        ctx,
        store,
        drvPath, // Use derivation path
        true,    // flip_direction - get reverse dependencies
        true,    // include_outputs
        false,   // include_derivers
        &data,
        [](nix_c_context * context, void * userdata, const StorePath * path) {
            auto * data = static_cast<CallbackData *>(userdata);
            std::string path_str;
            nix_store_path_name(path, OBSERVE_STRING(path_str));
            auto [it, inserted] = data->paths->insert(path_str);
            ASSERT_TRUE(inserted) << "Duplicate path in closure: " << path_str;
        });
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // Verify the output path is NOT in the closure when direction is flipped
    std::string outPathName;
    nix_store_path_name(outPath, OBSERVE_STRING(outPathName));
    ASSERT_EQ(closure_paths.count(outPathName), 0) << "Output path should not be in closure when flip_direction=true";
}

TEST_F(NixApiStoreTestWithRealisedPath, nix_store_get_fs_closure_include_derivers)
{
    // Test closure computation with include_derivers on an output path
    // This should include the derivation that produced the output
    struct CallbackData
    {
        std::set<std::string> * paths;
    };

    std::set<std::string> closure_paths;
    CallbackData data{&closure_paths};

    auto ret = nix_store_get_fs_closure(
        ctx,
        store,
        outPath, // Use output path (not derivation)
        false,   // flip_direction
        false,   // include_outputs
        true,    // include_derivers - include the derivation
        &data,
        [](nix_c_context * context, void * userdata, const StorePath * path) {
            auto * data = static_cast<CallbackData *>(userdata);
            std::string path_str;
            nix_store_path_name(path, OBSERVE_STRING(path_str));
            auto [it, inserted] = data->paths->insert(path_str);
            ASSERT_TRUE(inserted) << "Duplicate path in closure: " << path_str;
        });
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // Verify the derivation path is in the closure
    // Deriver is nasty stateful, and this assertion is only guaranteed because
    // we're using an empty store as our starting point. Otherwise, if the
    // output happens to exist, the deriver could be anything.
    std::string drvPathName;
    nix_store_path_name(drvPath, OBSERVE_STRING(drvPathName));
    ASSERT_EQ(closure_paths.count(drvPathName), 1) << "Derivation should be in closure when include_derivers=true";
}

TEST_F(NixApiStoreTestWithRealisedPath, nix_store_realise_output_ordering)
{
    // Test that nix_store_realise returns outputs in alphabetical order by output name.
    // This test uses a CA derivation with 10 outputs in randomized input order
    // to verify that the callback order is deterministic and alphabetical.
    nix::experimentalFeatureSettings.set("extra-experimental-features", "ca-derivations");
    nix::settings.getWorkerSettings().substituters = {};

    auto * store = open_local_store();

    // Create a CA derivation with 10 outputs using proper placeholders
    auto outa_ph = nix::hashPlaceholder("outa");
    auto outb_ph = nix::hashPlaceholder("outb");
    auto outc_ph = nix::hashPlaceholder("outc");
    auto outd_ph = nix::hashPlaceholder("outd");
    auto oute_ph = nix::hashPlaceholder("oute");
    auto outf_ph = nix::hashPlaceholder("outf");
    auto outg_ph = nix::hashPlaceholder("outg");
    auto outh_ph = nix::hashPlaceholder("outh");
    auto outi_ph = nix::hashPlaceholder("outi");
    auto outj_ph = nix::hashPlaceholder("outj");

    std::string drvJson = R"({
        "version": 4,
        "name": "multi-output-test",
        "system": ")" + nix::settings.thisSystem.get()
                          + R"(",
        "builder": "/bin/sh",
        "args": ["-c", "echo a > $outa; echo b > $outb; echo c > $outc; echo d > $outd; echo e > $oute; echo f > $outf; echo g > $outg; echo h > $outh; echo i > $outi; echo j > $outj"],
        "env": {
            "builder": "/bin/sh",
            "name": "multi-output-test",
            "system": ")" + nix::settings.thisSystem.get()
                          + R"(",
            "outf": ")" + outf_ph
                          + R"(",
            "outd": ")" + outd_ph
                          + R"(",
            "outi": ")" + outi_ph
                          + R"(",
            "oute": ")" + oute_ph
                          + R"(",
            "outh": ")" + outh_ph
                          + R"(",
            "outc": ")" + outc_ph
                          + R"(",
            "outb": ")" + outb_ph
                          + R"(",
            "outg": ")" + outg_ph
                          + R"(",
            "outj": ")" + outj_ph
                          + R"(",
            "outa": ")" + outa_ph
                          + R"("
        },
        "inputs": {
          "drvs": {},
          "srcs": []
        },
        "outputs": {
            "outd": { "hashAlgo": "sha256", "method": "nar" },
            "outf": { "hashAlgo": "sha256", "method": "nar" },
            "outg": { "hashAlgo": "sha256", "method": "nar" },
            "outb": { "hashAlgo": "sha256", "method": "nar" },
            "outc": { "hashAlgo": "sha256", "method": "nar" },
            "outi": { "hashAlgo": "sha256", "method": "nar" },
            "outj": { "hashAlgo": "sha256", "method": "nar" },
            "outh": { "hashAlgo": "sha256", "method": "nar" },
            "outa": { "hashAlgo": "sha256", "method": "nar" },
            "oute": { "hashAlgo": "sha256", "method": "nar" }
        }
    })";

    auto * drv = nix_derivation_from_json(ctx, store, drvJson.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    auto * drvPath = nix_add_derivation(ctx, store, drv);
    assert_ctx_ok();
    ASSERT_NE(drvPath, nullptr);

    // Realise the derivation - capture the order outputs are returned
    std::map<std::string, nix::StorePath> outputs;
    std::vector<std::string> output_order;
    auto cb = LambdaAdapter{.fun = [&](const char * outname, const StorePath * outPath) {
        ASSERT_NE(outname, nullptr);
        ASSERT_NE(outPath, nullptr);
        output_order.push_back(outname);
        outputs.emplace(outname, outPath->path);
    }};

    auto ret = nix_store_realise(
        ctx, store, drvPath, static_cast<void *>(&cb), decltype(cb)::call_void<const char *, const StorePath *>);
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);
    ASSERT_EQ(outputs.size(), 10);

    // Verify outputs are returned in alphabetical order by output name
    std::vector<std::string> expected_order = {
        "outa", "outb", "outc", "outd", "oute", "outf", "outg", "outh", "outi", "outj"};
    ASSERT_EQ(output_order, expected_order) << "Outputs should be returned in alphabetical order by output name";

    // Now compute closure with include_outputs and collect paths in order
    struct CallbackData
    {
        std::vector<std::string> * paths;
    };

    std::vector<std::string> closure_paths;
    CallbackData data{&closure_paths};

    ret = nix_store_get_fs_closure(
        ctx,
        store,
        drvPath,
        false, // flip_direction
        true,  // include_outputs - include the outputs in the closure
        false, // include_derivers
        &data,
        [](nix_c_context * context, void * userdata, const StorePath * path) {
            auto * data = static_cast<CallbackData *>(userdata);
            std::string path_str;
            nix_store_path_name(path, OBSERVE_STRING(path_str));
            data->paths->push_back(path_str);
        });
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);

    // Should contain at least the derivation and 10 outputs
    ASSERT_GE(closure_paths.size(), 11);

    // Verify all outputs are present in the closure
    for (const auto & [outname, outPath] : outputs) {
        std::string outPathName = store->ptr->printStorePath(outPath);

        bool found = false;
        for (const auto & p : closure_paths) {
            // nix_store_path_name returns just the name part, so match against full path name
            if (outPathName.find(p) != std::string::npos) {
                found = true;
                break;
            }
        }
        ASSERT_TRUE(found) << "Output " << outname << " (" << outPathName << ") not found in closure";
    }

    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
    nix_store_free(store);
}

TEST_F(NixApiStoreTestWithRealisedPath, nix_store_get_fs_closure_error_propagation)
{
    // Test that errors in the callback abort the closure computation
    struct CallbackData
    {
        int * count;
    };

    int call_count = 0;
    CallbackData data{&call_count};

    auto ret = nix_store_get_fs_closure(
        ctx,
        store,
        drvPath, // Use derivation path
        false,   // flip_direction
        true,    // include_outputs
        false,   // include_derivers
        &data,
        [](nix_c_context * context, void * userdata, const StorePath * path) {
            auto * data = static_cast<CallbackData *>(userdata);
            (*data->count)++;
            // Set an error immediately
            nix_set_err_msg(context, NIX_ERR_UNKNOWN, "Test error");
        });

    // Should have aborted with error
    ASSERT_EQ(ret, NIX_ERR_UNKNOWN);
    ASSERT_EQ(call_count, 1); // Should have been called exactly once, then aborted
}

/**
 * @brief Helper function to load JSON from a test data file
 *
 * @param filename Relative path from _NIX_TEST_UNIT_DATA
 * @return JSON string contents of the file
 */
static std::string load_json_from_test_data(const char * filename)
{
    std::filesystem::path unitTestData = nix::getUnitTestData();
    std::ifstream t{unitTestData / filename};
    std::stringstream buffer;
    buffer << t.rdbuf();
    return buffer.str();
}

TEST_F(nix_api_store_test, nix_derivation_to_json_roundtrip)
{
    // Load JSON from test data
    auto originalJson = load_json_from_test_data("derivation/invariants/filled-in-deferred-empty-env-var-pre.json");

    // Parse to derivation
    auto * drv = nix_derivation_from_json(ctx, store, originalJson.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    // Convert back to JSON
    std::string convertedJson;
    auto ret = nix_derivation_to_json(ctx, drv, OBSERVE_STRING(convertedJson));
    assert_ctx_ok();
    ASSERT_EQ(ret, NIX_OK);
    ASSERT_FALSE(convertedJson.empty());

    // Parse both JSON strings to compare (ignoring whitespace differences)
    auto originalParsed = nlohmann::json::parse(originalJson);
    auto convertedParsed = nlohmann::json::parse(convertedJson);

    // Remove parts that will be different due to filling-in.
    originalParsed.at("outputs").erase("out");
    originalParsed.at("env").erase("out");
    convertedParsed.at("outputs").erase("out");
    convertedParsed.at("env").erase("out");

    // They should be equivalent
    ASSERT_EQ(originalParsed, convertedParsed);

    nix_derivation_free(drv);
}

TEST_F(nix_api_store_test, nix_derivation_store_round_trip)
{
    // Load a derivation from JSON
    auto json = load_json_from_test_data("derivation/invariants/filled-in-deferred-empty-env-var-pre.json");
    auto * drv = nix_derivation_from_json(ctx, store, json.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    // Add to store
    auto * drvPath = nix_add_derivation(ctx, store, drv);
    assert_ctx_ok();
    ASSERT_NE(drvPath, nullptr);

    // Retrieve from store
    auto * drv2 = nix_store_drv_from_store_path(ctx, store, drvPath);
    assert_ctx_ok();
    ASSERT_NE(drv2, nullptr);

    // The round trip should make the same derivation
    ASSERT_EQ(drv->drv, drv2->drv);

    nix_store_path_free(drvPath);
    nix_derivation_free(drv);
    nix_derivation_free(drv2);
}

TEST_F(nix_api_store_test, nix_derivation_clone)
{
    // Load a derivation from JSON
    auto json = load_json_from_test_data("derivation/invariants/filled-in-deferred-empty-env-var-pre.json");
    auto * drv = nix_derivation_from_json(ctx, store, json.c_str());
    assert_ctx_ok();
    ASSERT_NE(drv, nullptr);

    // Clone the derivation
    auto * drv2 = nix_derivation_clone(drv);
    ASSERT_NE(drv2, nullptr);

    // The clone should be equal
    ASSERT_EQ(drv->drv, drv2->drv);

    nix_derivation_free(drv);
    nix_derivation_free(drv2);
}

} // namespace nixC
