#include "nix/store/tests/https-store.hh"

#include <thread>

namespace nix::testing {

void HttpsBinaryCacheStoreTest::openssl(Strings args)
{
    runProgram("openssl", /*lookupPath=*/true, args);
}

void HttpsBinaryCacheStoreTest::SetUp()
{
    LibStoreNetworkTest::SetUp();

#ifdef _WIN32
    GTEST_SKIP() << "HTTPS store tests are not supported on Windows";
#endif

    tmpDir = createTempDir();
    cacheDir = tmpDir / "cache";
    delTmpDir = std::make_unique<AutoDelete>(tmpDir);

    localCacheStore =
        make_ref<LocalBinaryCacheStoreConfig>("file", cacheDir.string(), LocalBinaryCacheStoreConfig::Params{})
            ->openStore();

    caCert = tmpDir / "ca.crt";
    caKey = tmpDir / "ca.key";
    serverCert = tmpDir / "server.crt";
    serverKey = tmpDir / "server.key";
    clientCert = tmpDir / "client.crt";
    clientKey = tmpDir / "client.key";

    // clang-format off
    openssl({"ecparam", "-genkey", "-name", "prime256v1", "-out", caKey.string()});
    openssl({"req", "-new", "-x509", "-days", "1", "-key", caKey.string(), "-out", caCert.string(), "-subj", "/CN=TestCA"});
    openssl({"ecparam", "-genkey", "-name", "prime256v1", "-out", serverKey.string()});
    openssl({"req", "-new", "-key", serverKey.string(), "-out", (tmpDir / "server.csr").string(), "-subj", "/CN=localhost"});
    openssl({"x509", "-req", "-in", (tmpDir / "server.csr").string(), "-CA", caCert.string(), "-CAkey", caKey.string(), "-CAcreateserial", "-out", serverCert.string(), "-days", "1"});
    openssl({"ecparam", "-genkey", "-name", "prime256v1", "-out", clientKey.string()});
    openssl({"req", "-new", "-key", clientKey.string(), "-out", (tmpDir / "client.csr").string(), "-subj", "/CN=TestClient"});
    openssl({"x509", "-req", "-in", (tmpDir / "client.csr").string(), "-CA", caCert.string(), "-CAkey", caKey.string(), "-CAcreateserial", "-out", clientCert.string(), "-days", "1"});
    // clang-format on

#ifndef _WIN32 /* FIXME: Can't yet start processes on windows */
    auto args = serverArgs();
    serverPid = startProcess(
        [&] {
            if (chdir(cacheDir.c_str()) == -1)
                _exit(1);
            std::vector<char *> argv;
            argv.push_back(const_cast<char *>("openssl"));
            for (auto & a : args)
                argv.push_back(const_cast<char *>(a.c_str()));
            argv.push_back(nullptr);
            execvp("openssl", argv.data());
            _exit(1);
        },
        {.dieWithParent = true});
#endif

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
    serverPid.kill();
    delTmpDir.reset();
    testFileTransferSettings.reset();
}

std::vector<std::string> HttpsBinaryCacheStoreTest::serverArgs()
{
    return {
        "s_server",
        "-accept",
        std::to_string(port),
        "-cert",
        serverCert.string(),
        "-key",
        serverKey.string(),
        "-WWW", /* Serve from current directory. */
        "-quiet",
    };
}

std::vector<std::string> HttpsBinaryCacheStoreMtlsTest::serverArgs()
{
    auto args = HttpsBinaryCacheStoreTest::serverArgs();
    /* With the -Verify option the client must supply a certificate or an error occurs, which is not the
       case with -verify. */
    args.insert(args.end(), {"-CAfile", caCert.string(), "-Verify", "1", "-verify_return_error"});
    return args;
}

ref<HttpBinaryCacheStoreConfig> HttpsBinaryCacheStoreTest::makeConfig()
{
    auto res = make_ref<HttpBinaryCacheStoreConfig>(
        ParsedURL{
            .scheme = "https",
            .authority =
                ParsedURL::Authority{
                    .host = "localhost",
                    .port = port,
                },
        },
        HttpBinaryCacheStoreConfig::Params{});
    res->pathInfoCacheSize = 0; /* We don't want any caching in tests. */
    return res;
}

ref<HttpBinaryCacheStore> HttpsBinaryCacheStoreTest::openStore(ref<HttpBinaryCacheStoreConfig> config)
{
    /* Create the store directly without the DiskCachedBinaryCacheStore wrapper,
       since we don't want disk caching in tests. */
    auto store = make_ref<HttpBinaryCacheStore>(config, ref<FileTransfer>{testFileTransfer});
    store->init();
    return store;
}

} // namespace nix::testing
