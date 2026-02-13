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
    Pid serverPid;
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
    ref<HttpBinaryCacheStore::Config> makeConfig();
    ref<HttpBinaryCacheStore> openStore(ref<HttpBinaryCacheStore::Config> config);
};

class HttpsBinaryCacheStoreMtlsTest : public HttpsBinaryCacheStoreTest
{
protected:
    std::vector<std::string> serverArgs() override;
};

} // namespace nix::testing
