#include "nix/store/tests/https-store.hh"
#include "nix/util/os-string.hh"

#include <thread>

namespace nix::testing {

void TestHttpBinaryCacheStore::init()
{
    BinaryCacheStore::init();
}

ref<TestHttpBinaryCacheStore> TestHttpBinaryCacheStoreConfig::openTestStore(ref<FileTransfer> fileTransfer) const
{
    auto store = make_ref<TestHttpBinaryCacheStore>(
        ref{// FIXME we shouldn't actually need a mutable config
            std::const_pointer_cast<HttpBinaryCacheStore::Config>(shared_from_this())},
        fileTransfer);
    store->init();
    return store;
}

void HttpsBinaryCacheStoreTest::openssl(Strings args)
{
    runProgram("openssl", /*lookupPath=*/true, toOsStrings(std::move(args)));
}

void HttpsBinaryCacheStoreTest::SetUp()
{
    LibStoreNetworkTest::SetUp();

    /* A skip in the base returns from the base only, so guard the setup below. */
    if (IsSkipped())
        return;

    tmpDir = createTempDir();
    cacheDir = tmpDir / "cache";
    delTmpDir = std::make_unique<AutoDelete>(tmpDir);

    localCacheStore =
        make_ref<LocalBinaryCacheStoreConfig>(cacheDir, LocalBinaryCacheStoreConfig::Params{})->openStore();

    caCert = tmpDir / "ca.crt";
    caKey = tmpDir / "ca.key";
    serverCert = tmpDir / "server.crt";
    serverKey = tmpDir / "server.key";
    clientCert = tmpDir / "client.crt";
    clientKey = tmpDir / "client.key";

    // clang-format off
    openssl({"ecparam", "-genkey", "-name", "prime256v1", "-out", caKey.string()});
    openssl({"req", "-new", "-x509", "-days", "1", "-key", caKey.string(), "-out", caCert.string(), "-subj", "/CN=TestCA"});
    auto serverExtFile = tmpDir / "server.ext";
    writeFile(serverExtFile, "subjectAltName=DNS:localhost,IP:127.0.0.1");
    openssl({"ecparam", "-genkey", "-name", "prime256v1", "-out", serverKey.string()});
    openssl({"req", "-new", "-key", serverKey.string(), "-out", (tmpDir / "server.csr").string(), "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost,IP:127.0.0.1"});
    openssl({"x509", "-req", "-in", (tmpDir / "server.csr").string(), "-CA", caCert.string(), "-CAkey", caKey.string(), "-CAcreateserial", "-out", serverCert.string(), "-days", "1", "-extfile", serverExtFile.string()});
    openssl({"ecparam", "-genkey", "-name", "prime256v1", "-out", clientKey.string()});
    openssl({"req", "-new", "-key", clientKey.string(), "-out", (tmpDir / "client.csr").string(), "-subj", "/CN=TestClient"});
    openssl({"x509", "-req", "-in", (tmpDir / "client.csr").string(), "-CA", caCert.string(), "-CAkey", caKey.string(), "-CAcreateserial", "-out", clientCert.string(), "-days", "1"});
    // clang-format on

    serverPid = spawnProgram(
        {
            .program = OS_STR("openssl"),
            .lookupPath = true,
            .args = serverArgs(),
            .chdir = cacheDir,
        },
        /*fdr=*/{});

    /* As an optimization, sleep for a bit to allow the server to come up to avoid retrying when connecting.
       This won't make the tests fail, but does make them run faster. We don't need to overcomplicate by waiting
       for the port explicitly - this is enough. */
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    /* Create custom FileTransferSettings with our test CA certificate.
       This avoids mutating global settings. */
    testFileTransferSettings = std::make_unique<FileTransferSettings>();
    testFileTransferSettings->caFile = caCert;
    testFileTransfer = makeFileTransfer(*testFileTransferSettings);
}

void HttpsBinaryCacheStoreTest::TearDown()
{
    if (serverPid)
        serverPid.kill();
    delTmpDir.reset();
    testFileTransferSettings.reset();
}

OsStrings HttpsBinaryCacheStoreTest::serverArgs()
{
    return {
        OS_STR("s_server"),
        OS_STR("-accept"),
        string_to_os_string(std::to_string(port)),
        OS_STR("-cert"),
        serverCert.native(),
        OS_STR("-key"),
        serverKey.native(),
        OS_STR("-WWW"), /* Serve from current directory. */
        OS_STR("-quiet"),
    };
}

OsStrings HttpsBinaryCacheStoreMtlsTest::serverArgs()
{
    auto args = HttpsBinaryCacheStoreTest::serverArgs();
    /* With the -Verify option the client must supply a certificate or an error occurs, which is not the
       case with -verify. */
    args.insert(
        args.end(),
        {
            OS_STR("-CAfile"),
            caCert.native(),
            OS_STR("-Verify"),
            OS_STR("1"),
            OS_STR("-verify_return_error"),
        });
    return args;
}

ref<TestHttpBinaryCacheStoreConfig> HttpsBinaryCacheStoreTest::makeConfig()
{
    auto res = make_ref<TestHttpBinaryCacheStoreConfig>(
        ParsedURL{
            .scheme = "https",
            .authority =
                ParsedURL::Authority{
                    .host = "localhost",
                    .port = port,
                },
        },
        TestHttpBinaryCacheStoreConfig::Params{});
    res->pathInfoCacheSize = 0; /* We don't want any caching in tests. */
    return res;
}

ref<TestHttpBinaryCacheStore> HttpsBinaryCacheStoreTest::openStore(ref<TestHttpBinaryCacheStoreConfig> config)
{
    return config->openTestStore(ref<FileTransfer>{testFileTransfer});
}

} // namespace nix::testing
