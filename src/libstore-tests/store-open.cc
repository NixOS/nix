#include <gtest/gtest.h>

#include "nix/store/store-open.hh"
#include "nix/store/store-reference.hh"
#include "nix/store/local-store.hh"
#include "nix/store/dummy-store.hh"
#include "nix/store/legacy-ssh-store.hh"
#include "nix/store/local-binary-cache-store.hh"
#include "nix/store/store-registration.hh"
#include "nix/store/uds-remote-store.hh"
#include "nix/store/globals.hh"
#include "nix/util/file-system.hh"
#include "nix/util/finally.hh"
#include "nix/util/terminal.hh"

namespace nix {

namespace {

struct TestAuthorityStoreConfig : LegacySSHStoreConfig
{
    TestAuthorityStoreConfig(const Params & params)
        : TestAuthorityStoreConfig(ParsedURL::Authority{}, params)
    {
    }

    TestAuthorityStoreConfig(const ParsedURL::Authority & authority, const Params & params)
        : StoreConfig(params, FilePathType::Unix)
        , CommonSSHStoreConfig(authority, params)
        , LegacySSHStoreConfig(authority, params)
    {
    }

    static std::string name()
    {
        return "Test authority store";
    }

    static StringSet uriSchemes()
    {
        return {"test-authority"};
    }
};

} // namespace

TEST(StoreOpen, resolveStoreConfig_auto_default)
{
    // Save original settings
    //
    // TODO: resolveStoreConfig should not depend on global settings;
    // the test should not have to override them.
    auto originalStateDir = settings.nixStateDir;
    Finally restoreStateDir([&]() { settings.nixStateDir = originalStateDir; });

    // Set up a temporary writable state directory
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto stateDir = tmpDir / "var/nix";
    createDirs(stateDir);
    settings.nixStateDir = stateDir;

    StoreReference ref{
        .variant = StoreReference::Auto{},
        .params = {},
    };

    auto config = resolveStoreConfig(std::move(ref));

    // With a writable state directory and no daemon socket, "auto" should resolve to LocalStore
    auto * localConfig = dynamic_cast<LocalStore::Config *>(config.get());
    ASSERT_NE(localConfig, nullptr);
    EXPECT_EQ(localConfig->getStateDir(), stateDir);
}

TEST(StoreOpen, resolveStoreConfig_auto_withParams)
{
    // Create a temporary directory with a writable state directory
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto stateDir = tmpDir / "var/nix";
    createDirs(stateDir);

    StoreReference ref{
        .variant = StoreReference::Auto{},
        .params = {{"state", stateDir.string()}},
    };

    auto config = resolveStoreConfig(std::move(ref));

    // With a writable state directory and no daemon socket, "auto" should resolve to LocalStore
    auto * localConfig = dynamic_cast<LocalStore::Config *>(config.get());
    ASSERT_NE(localConfig, nullptr);
    EXPECT_EQ(localConfig->getStateDir(), stateDir);
}

TEST(StoreOpen, networkStoreRequiresAuthority)
{
    for (auto scheme : {"ssh", "ssh-ng", "mounted-ssh-ng", "http", "https", "s3"}) {
        for (auto address : {
                 "",
                 "127.0.0.1",
                 "localhost",
                 "user@localhost",
                 "[::1]",
                 "::1",
                 "user@[::1]",
                 "user@::1",
                 "[fe80::1%eth0]",
                 "fe80::1%eth0",
                 "/localhost",
                 "/[::1]",
             }) {
            for (auto query : {"", "?a=b", "?remote-store=local://"}) {
                auto uri = std::string{scheme} + ":" + address + query;
                SCOPED_TRACE(uri);
                EXPECT_THROW(resolveStoreConfig(StoreReference::parse(uri)), UsageError);
            }
        }
    }
}

TEST(StoreOpen, invalidStoreReferenceError)
{
    for (auto uri : {"ssh:127.0.0.1", "ssh:localhost", "ssh-ng:[::1]", "not-a-store"}) {
        SCOPED_TRACE(uri);
        try {
            resolveStoreConfig(StoreReference::parse(uri));
            FAIL() << "Expected a store reference parse error";
        } catch (const UsageError & e) {
            EXPECT_EQ(
                filterANSIEscapes(e.message(), /*filterAll=*/true),
                "Failed to parse store reference: '" + std::string{uri} + "'");
        }
    }
}

TEST(StoreOpen, customStoreReceivesParsedURL)
{
    std::optional<ParsedURL> receivedURL;
    StoreReference::Params receivedParams;
    auto & factories = Implementations::registered();
    auto [it, inserted] = factories.emplace(
        "Test URL store",
        StoreFactory{
            .uriSchemes = {"test+cache"},
            .parseConfig = [&](const ParsedURL & uri, const StoreReference::Params & params) -> ref<StoreConfig> {
                receivedURL = uri;
                receivedParams = params;
                return make_ref<DummyStoreConfig>(StoreReference::Params{});
            },
            .getConfig = []() -> ref<StoreConfig> { return make_ref<DummyStoreConfig>(StoreReference::Params{}); },
        });
    ASSERT_TRUE(inserted);
    Finally unregister([&] { factories.erase(it); });

    for (std::string uri : {
             "test+cache:relative/path%2Fsegment",
             "test+cache:/absolute/path",
             "test+cache:///absolute/path",
             "test+cache://host/path",
         }) {
        SCOPED_TRACE(uri);
        receivedURL.reset();
        auto reference = StoreReference::parse(uri + "?a=original&b=retained#fragment", {{"a", "override"}});
        auto config = resolveStoreConfig(std::move(reference));
        ASSERT_TRUE(receivedURL);
        EXPECT_EQ(*receivedURL, parseURL(uri + "#fragment"));
        EXPECT_EQ(receivedParams, (StoreReference::Params{{"a", "override"}, {"b", "retained"}}));
    }
}

TEST(StoreOpen, customAuthorityStoreRequiresAuthority)
{
    Implementations::add<TestAuthorityStoreConfig>();
    Finally unregister([&] { Implementations::registered().erase(TestAuthorityStoreConfig::name()); });

    for (auto uri : {"test-authority:localhost", "test-authority:/localhost", "test-authority://localhost/path"}) {
        SCOPED_TRACE(uri);
        EXPECT_THROW(resolveStoreConfig(StoreReference::parse(uri)), UsageError);
    }

    auto config = resolveStoreConfig(StoreReference::parse("test-authority://user@localhost:2222"));
    auto * customConfig = dynamic_cast<TestAuthorityStoreConfig *>(config.get());
    ASSERT_NE(customConfig, nullptr);
    EXPECT_EQ(customConfig->authority.host, "localhost");
    EXPECT_EQ(customConfig->authority.user, "user");
    EXPECT_EQ(customConfig->authority.port, 2222);
}

TEST(StoreOpen, filesystemStoreShorthand)
{
    auto tmpDir = createTempDir();
    AutoDelete cleanup(tmpDir, true);
    auto path = encodeUrlPath(pathToUrlPath(tmpDir));
    for (auto scheme : {"local", "unix", "file"}) {
        auto prefix = std::string{scheme} + ":";
        auto shortConfig = resolveStoreConfig(StoreReference::parse(prefix + path));
        auto fullConfig = resolveStoreConfig(StoreReference::parse(prefix + "//" + path));
        EXPECT_EQ(shortConfig->getReference(), fullConfig->getReference());
        if (auto * local = dynamic_cast<LocalStoreConfig *>(shortConfig.get()))
            EXPECT_EQ(local->rootDir.get(), tmpDir);
        else if (auto * unix = dynamic_cast<UDSRemoteStoreConfig *>(shortConfig.get()))
            EXPECT_EQ(unix->path, tmpDir);
        else if (auto * file = dynamic_cast<LocalBinaryCacheStoreConfig *>(shortConfig.get()))
            EXPECT_EQ(file->binaryCacheDir, tmpDir);
        else
            FAIL() << "Unexpected store configuration for " << scheme;
    }
}

} // namespace nix
