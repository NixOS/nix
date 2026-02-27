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

    TestHttpBinaryCacheStore(ref<HttpBinaryCacheStoreConfig> config, ref<FileTransfer> fileTransfer)
        : Store{*config}
        , BinaryCacheStore{*config}
        , HttpBinaryCacheStore(config, fileTransfer)
    {
        diskCache = nullptr; /* Disable caching, we'll be creating a new binary cache for each test. */
    }

    void init() override;
};

class TestHttpBinaryCacheStoreConfig : public HttpBinaryCacheStoreConfig
{
public:
    TestHttpBinaryCacheStoreConfig(ParsedURL url, const Store::Config::Params & params)
        : StoreConfig(params, FilePathType::Unix)
        , HttpBinaryCacheStoreConfig(url, params)
    {
    }

    ref<TestHttpBinaryCacheStore> openTestStore(ref<FileTransfer> fileTransfer) const;
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
#ifndef _WIN32 /* FIXME: Can't yet start background processes on windows */
    Pid serverPid;
#endif
    uint16_t port = 8443;
    std::shared_ptr<Store> localCacheStore;

    /**
     * Custom FileTransferSettings with the test CA certificate.
     * This is used instead of modifying global settings.
     */
    std::unique_ptr<FileTransferSettings> testFileTransferSettings;

    /**
     * FileTransfer instance using our test settings.
     * Initialized in SetUp().
     */
    std::shared_ptr<FileTransfer> testFileTransfer;

    static void openssl(Strings args);
    void SetUp() override;
    void TearDown() override;

    virtual std::vector<std::string> serverArgs();
    ref<TestHttpBinaryCacheStoreConfig> makeConfig();
    ref<TestHttpBinaryCacheStore> openStore(ref<TestHttpBinaryCacheStoreConfig> config);
};

class HttpsBinaryCacheStoreMtlsTest : public HttpsBinaryCacheStoreTest
{
protected:
    std::vector<std::string> serverArgs() override;
};

} // namespace nix::testing
