#include <gtest/gtest.h>
#include <regex>

#include "nix/store/http-binary-cache-store.hh"
#include "nix/store/tests/https-store.hh"
#include "nix/util/fs-sink.hh"

namespace nix {

TEST(HttpBinaryCacheStore, constructConfig)
{
    HttpBinaryCacheStoreConfig config{"http", "foo.bar.baz", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "http://foo.bar.baz");
}

TEST(HttpBinaryCacheStore, constructConfigNoTrailingSlash)
{
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b/", {}};

    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b");
}

TEST(HttpBinaryCacheStore, constructConfigWithParams)
{
    StoreConfig::Params params{{"compression", "xz"}};
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b/", params};
    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b");
    EXPECT_EQ(config.getReference().params, params);
}

TEST(HttpBinaryCacheStore, constructConfigWithParamsAndUrlWithParams)
{
    StoreConfig::Params params{{"compression", "xz"}};
    HttpBinaryCacheStoreConfig config{"https", "foo.bar.baz/a/b?some-param=some-value", params};
    EXPECT_EQ(config.cacheUri.to_string(), "https://foo.bar.baz/a/b?some-param=some-value");
    EXPECT_EQ(config.getReference().params, params);
}

using testing::HttpsBinaryCacheStoreMtlsTest;
using testing::HttpsBinaryCacheStoreTest;

using namespace std::string_view_literals;
using namespace std::string_literals;

TEST_F(HttpsBinaryCacheStoreTest, queryPathInfo)
{
    auto config = makeConfig({});
    auto store = config->openStore();
    StringSource dump{"test"sv};
    auto path = localCacheStore->addToStoreFromDump(dump, "test-name", FileSerialisationMethod::Flat);
    EXPECT_NO_THROW(store->queryPathInfo(path));
}

auto withNoRetries()
{
    auto oldTries = fileTransferSettings.tries.get();
    Finally restoreTries = [=]() { fileTransferSettings.tries = oldTries; };
    fileTransferSettings.tries = 1; /* FIXME: Don't use global settings. */
    return restoreTries;
}

TEST_F(HttpsBinaryCacheStoreMtlsTest, queryPathInfo)
{
    auto config = makeConfig({
        {"tls-certificate"s, clientCert.string()},
        {"tls-private-key"s, clientKey.string()},
    });
    auto store = config->openStore();
    StringSource dump{"test"sv};
    auto path = localCacheStore->addToStoreFromDump(dump, "test-name", FileSerialisationMethod::Flat);
    EXPECT_NO_THROW(store->queryPathInfo(path));
}

TEST_F(HttpsBinaryCacheStoreMtlsTest, rejectsWithoutClientCert)
{
    auto restoreTries = withNoRetries();
    auto config = makeConfig({});
    EXPECT_THROW(config->openStore(), Error);
}

TEST_F(HttpsBinaryCacheStoreMtlsTest, rejectsWrongClientCert)
{
    auto wrongKey = tmpDir / "wrong.key";
    auto wrongCert = tmpDir / "wrong.crt";

    // clang-format off
    openssl({"ecparam", "-genkey", "-name", "prime256v1", "-out", wrongKey.string()});
    openssl({"req", "-new", "-x509", "-days", "1", "-key", wrongKey.string(), "-out", wrongCert.string(), "-subj", "/CN=WrongClient"});
    // clang-format on

    auto config = makeConfig({
        {"tls-certificate"s, wrongCert.string()},
        {"tls-private-key"s, wrongKey.string()},
    });
    auto restoreTries = withNoRetries();
    EXPECT_THROW(config->openStore(), Error);
}

TEST_F(HttpsBinaryCacheStoreMtlsTest, doesNotSendCertOnRedirectToDifferentAuthority)
{
    StringSource dump{"test"sv};
    auto path = localCacheStore->addToStoreFromDump(dump, "test-name", FileSerialisationMethod::Flat);

    for (auto & entry : DirectoryIterator{cacheDir})
        if (entry.path().extension() == ".narinfo") {
            auto content = readFile(entry.path());
            content = std::regex_replace(content, std::regex("URL: nar/"), fmt("URL: https://127.0.0.1:%d/nar/", port));
            writeFile(entry.path(), content);
        }

    auto config = makeConfig({
        {"tls-certificate"s, clientCert.string()},
        {"tls-private-key"s, clientKey.string()},
    });
    auto store = config->openStore();

    auto restoreTries = withNoRetries();
    auto info = store->queryPathInfo(path);
    NullSink null;
    EXPECT_THROW(store->narFromPath(path, null), Error);
}

} // namespace nix
