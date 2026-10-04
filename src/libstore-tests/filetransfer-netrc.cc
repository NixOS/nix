#include <gtest/gtest.h>

#include "nix/store/filetransfer-impl.hh"
#include "nix/store/tests/secret-resolver.hh"
#include "nix/util/file-system.hh"

namespace nix {

namespace {

FileTransferRequest getRequest(std::string url = "https://cache.example.org/nar/abc")
{
    return FileTransferRequest(VerbatimURL{std::move(url)});
}

/** A `netrc-file` setting pointing somewhere recognisable. */
FileTransferSettings settingsWithNetrc(const std::filesystem::path & path)
{
    FileTransferSettings settings;
    settings.netrcFile = path;
    return settings;
}

SecretPurpose buildPurpose()
{
    return SecretPurpose{.consumer = "builtin:fetchurl", .operation = "build"};
}

} // namespace

TEST(ResolveNetrcFile, withoutResolverUsesSetting)
{
    auto settings = settingsWithNetrc("/etc/nix/netrc");

    auto netrc = resolveNetrcFile(FileTransferContext{}, settings, getRequest());

    EXPECT_EQ(netrc.path, "/etc/nix/netrc");
    /* Nothing to keep alive: the setting names a file we don't own. */
    EXPECT_EQ(netrc.lease, nullptr);
}

TEST(ResolveNetrcFile, resolverWithoutNetrcFallsBackToSetting)
{
    auto settings = settingsWithNetrc("/etc/nix/netrc");
    auto resolver =
        std::make_shared<testing::CallbackSecretResolver>([](const SecretRequest &) { return std::nullopt; });

    auto netrc = resolveNetrcFile(FileTransferContext{.secretResolver = resolver}, settings, getRequest());

    EXPECT_EQ(netrc.path, "/etc/nix/netrc");
    EXPECT_EQ(netrc.lease, nullptr);
    /* The resolver was consulted; it just had nothing to offer. */
    ASSERT_EQ(resolver->requests.size(), 1u);
}

TEST(ResolveNetrcFile, resolverFileTakesPrecedenceOverSetting)
{
    auto settings = settingsWithNetrc("/etc/nix/netrc");
    auto resolver = std::make_shared<testing::CallbackSecretResolver>([](const SecretRequest &) {
        return ResolvedSecret{.value = make_ref<testing::CallbackSecretFile>("/run/secrets/netrc")};
    });

    auto netrc = resolveNetrcFile(FileTransferContext{.secretResolver = resolver}, settings, getRequest());

    EXPECT_EQ(netrc.path, "/run/secrets/netrc");
    ASSERT_NE(netrc.lease, nullptr);
    EXPECT_EQ(netrc.lease->path(), "/run/secrets/netrc");
}

TEST(ResolveNetrcFile, scopesRequestToHostAndOperation)
{
    auto settings = settingsWithNetrc("/etc/nix/netrc");
    auto resolver =
        std::make_shared<testing::CallbackSecretResolver>([](const SecretRequest &) { return std::nullopt; });

    auto request = getRequest("https://cache.example.org:8443/nar/abc");
    request.method = HttpMethod::Head;
    resolveNetrcFile(FileTransferContext{.secretResolver = resolver}, settings, request);

    ASSERT_EQ(resolver->requests.size(), 1u);
    const auto & secretRequest = resolver->requests.at(0);
    EXPECT_EQ(secretRequest.name, "netrc");
    /* curl reads netrc from disk, so an inline value would be useless. */
    EXPECT_EQ(secretRequest.representation, SecretRepresentation::MaterialisedFile);
    EXPECT_EQ(secretRequest.purpose.consumer, "file-transfer");
    EXPECT_EQ(secretRequest.purpose.operation, "download");
    /* The port is not part of a netrc machine name. */
    EXPECT_EQ(secretRequest.purpose.host, std::optional<std::string>{"cache.example.org"});
}

TEST(ResolveNetrcFile, unparseableUrlLeavesHostUnscoped)
{
    auto settings = settingsWithNetrc("/etc/nix/netrc");
    auto resolver =
        std::make_shared<testing::CallbackSecretResolver>([](const SecretRequest &) { return std::nullopt; });

    resolveNetrcFile(FileTransferContext{.secretResolver = resolver}, settings, getRequest("not a url"));

    ASSERT_EQ(resolver->requests.size(), 1u);
    EXPECT_EQ(resolver->requests.at(0).purpose.host, std::nullopt);
}

TEST(ResolveNetrcFile, inlineSecretIsRejected)
{
    auto settings = settingsWithNetrc("/etc/nix/netrc");
    auto resolver = std::make_shared<testing::CallbackSecretResolver>(
        [](const SecretRequest &) { return ResolvedSecret{.value = InlineSecret{"machine example.org"}}; });

    EXPECT_THROW(resolveNetrcFile(FileTransferContext{.secretResolver = resolver}, settings, getRequest()), Error);
}

TEST(ResolveNetrcFile, leaseOutlivesResolutionAndIsReleasedWithIt)
{
    auto settings = settingsWithNetrc("/etc/nix/netrc");
    bool released = false;
    auto resolver = std::make_shared<testing::CallbackSecretResolver>([&](const SecretRequest &) {
        return ResolvedSecret{
            .value = make_ref<testing::CallbackSecretFile>("/run/secrets/netrc", [&] { released = true; })};
    });

    {
        auto netrc = resolveNetrcFile(FileTransferContext{.secretResolver = resolver}, settings, getRequest());
        /* The materialisation must survive the resolve() call itself: curl
           reads the file long after we hand it the path. */
        EXPECT_FALSE(released);
        EXPECT_EQ(netrc.path, "/run/secrets/netrc");
    }

    EXPECT_TRUE(released);
}

TEST(ResolveNetrcData, resolverValueTakesPrecedenceOverSetting)
{
    auto settings = settingsWithNetrc("/definitely/not/a/netrc");
    auto resolver = std::make_shared<testing::CallbackSecretResolver>(
        [](const SecretRequest &) { return ResolvedSecret{.value = InlineSecret{"machine example.org"}}; });

    auto data = resolveNetrcData(resolver, settings, buildPurpose());

    ASSERT_TRUE(data);
    EXPECT_EQ(*data, "machine example.org");

    ASSERT_EQ(resolver->requests.size(), 1u);
    const auto & request = resolver->requests.at(0);
    EXPECT_EQ(request.name, "netrc");
    /* The bytes have to cross into a sandbox, so a leased file is no use. */
    EXPECT_EQ(request.representation, SecretRepresentation::Inline);
    EXPECT_EQ(request.purpose.consumer, "builtin:fetchurl");
    /* One netrc serves every URL the build tries, so it is not host-scoped. */
    EXPECT_EQ(request.purpose.host, std::nullopt);
}

TEST(ResolveNetrcData, resolverWithoutNetrcFallsBackToSettingFile)
{
    AutoDelete tmpDir(createTempDir());
    auto netrcPath = tmpDir.path() / "netrc";
    writeFile(netrcPath, "machine fallback.example.org", 0600);

    auto settings = settingsWithNetrc(netrcPath);
    auto resolver =
        std::make_shared<testing::CallbackSecretResolver>([](const SecretRequest &) { return std::nullopt; });

    auto data = resolveNetrcData(resolver, settings, buildPurpose());

    ASSERT_TRUE(data);
    EXPECT_EQ(*data, "machine fallback.example.org");
    /* The resolver was asked; the setting is only the fallback. */
    ASSERT_EQ(resolver->requests.size(), 1u);
}

TEST(ResolveNetrcData, unreadableSettingYieldsNothing)
{
    auto settings = settingsWithNetrc("/definitely/not/a/netrc");

    EXPECT_EQ(resolveNetrcData(nullptr, settings, buildPurpose()), std::nullopt);
}

TEST(ResolveNetrcData, explicitlyEmptyResolverValueTakesPrecedenceOverSetting)
{
    AutoDelete tmpDir(createTempDir());
    auto netrcPath = tmpDir.path() / "netrc";
    writeFile(netrcPath, "machine fallback.example.org", 0600);

    auto settings = settingsWithNetrc(netrcPath);
    auto resolver = std::make_shared<testing::CallbackSecretResolver>(
        [](const SecretRequest &) { return ResolvedSecret{.value = InlineSecret{""}}; });

    auto data = resolveNetrcData(resolver, settings, buildPurpose());

    ASSERT_TRUE(data);
    EXPECT_TRUE(data->empty());
}

TEST(ResolveNetrcData, materialisedFileIsRejected)
{
    auto settings = settingsWithNetrc("/definitely/not/a/netrc");
    auto resolver = std::make_shared<testing::CallbackSecretResolver>([](const SecretRequest &) {
        return ResolvedSecret{.value = make_ref<testing::CallbackSecretFile>("/run/secrets/netrc")};
    });

    EXPECT_THROW(resolveNetrcData(resolver, settings, buildPurpose()), Error);
}

} // namespace nix
