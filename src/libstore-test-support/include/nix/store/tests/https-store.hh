#pragma once
///@file

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/store/tests/libstore-network.hh"
#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/store-api.hh"
#include "nix/store/globals.hh"
#include "nix/store/local-binary-cache-store.hh"
#include "nix/util/file-system.hh"
#include "nix/util/processes.hh"

namespace nix::testing {

class TestHttpBinaryCacheStoreConfig;

/**
 * Test shim for testing. We don't want to use the on-disk narinfo cache in unit
 * tests.
 */
class TestHttpBinaryCacheStore : public HttpBinaryCacheStore
{
public:
    TestHttpBinaryCacheStore(const TestHttpBinaryCacheStore &) = delete;
    TestHttpBinaryCacheStore(TestHttpBinaryCacheStore &&) = delete;
    TestHttpBinaryCacheStore & operator=(const TestHttpBinaryCacheStore &) = delete;
    TestHttpBinaryCacheStore & operator=(TestHttpBinaryCacheStore &&) = delete;

    TestHttpBinaryCacheStore(ref<HttpBinaryCacheStoreConfig> config)
        : Store{*config}
        , BinaryCacheStore{*config}
        , HttpBinaryCacheStore(config)
    {
        diskCache = nullptr; /* Disable caching, we'll be creating a new binary cache for each test. */
    }

    void init() override;
};

class TestHttpBinaryCacheStoreConfig : public HttpBinaryCacheStoreConfig
{
public:
    TestHttpBinaryCacheStoreConfig(
        std::string_view scheme, std::string_view cacheUri, const Store::Config::Params & params)
        : StoreConfig(params)
        , HttpBinaryCacheStoreConfig(scheme, cacheUri, params)
    {
    }

    ref<TestHttpBinaryCacheStore> openTestStore() const;

    ref<Store> openStore() const override
    {
        return openTestStore();
    }
};

class HttpsBinaryCacheStoreTest : public virtual LibStoreNetworkTest
{
    std::unique_ptr<AutoDelete> delTmpDir;

public:
    static void SetUpTestSuite()
    {
        initLibStore(/*loadConfig=*/false);
    }

protected:
    std::filesystem::path tmpDir, cacheDir;
    std::filesystem::path caCert, caKey, serverCert, serverKey;
    std::filesystem::path clientCert, clientKey;
    std::optional<std::filesystem::path> oldCaCert;
    Pid serverPid;
    uint16_t port = 8443;
    std::shared_ptr<Store> localCacheStore;

    static void openssl(Strings args);
    void SetUp() override;
    void TearDown() override;

    virtual std::vector<std::string> serverArgs();
    ref<TestHttpBinaryCacheStoreConfig> makeConfig(BinaryCacheStoreConfig::Params params);
};

class HttpsBinaryCacheStoreMtlsTest : public HttpsBinaryCacheStoreTest
{
protected:
    std::vector<std::string> serverArgs() override;
};

} // namespace nix::testing
