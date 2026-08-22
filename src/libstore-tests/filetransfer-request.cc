#include <gtest/gtest.h>

#include "nix/store/filetransfer.hh"
#include "nix/store/tests/secretspec.hh"
#include "nix/util/file-system.hh"

namespace nix {

TEST(FileTransferRequest, displayUriStripsUserinfo)
{
    FileTransferRequest req(VerbatimURL{std::string{"https://alice:s3cr3t@example.org:8443/path/file.toml?x=1"}});
    // uri itself is untouched (used for CURLOPT_URL, result.urls, cache keys).
    EXPECT_EQ(req.uri.to_string(), "https://alice:s3cr3t@example.org:8443/path/file.toml?x=1");
    // displayUri() drops the userinfo for diagnostics.
    EXPECT_EQ(req.displayUri(), "https://example.org:8443/path/file.toml?x=1");

    FileTransferRequest plain(VerbatimURL{std::string{"https://example.org/file"}});
    EXPECT_EQ(plain.displayUri(), "https://example.org/file");
}

TEST(FileTransferSettings, doesNotResolveSecretSpecNetrcForFileUrls)
{
    auto dir = createTempDir();
    AutoDelete cleanup{dir};
    auto source = dir / "source";
    writeFile(source, "local data");

    SecretSpecSettings secretSettings;
    FileTransferSettings transferSettings{secretSettings};
    transferSettings.secretSpecNetrcFile = "UNUSED_NETRC_SECRET";

    auto result =
        makeFileTransfer(transferSettings)->download(FileTransferRequest{VerbatimURL{"file://" + source.string()}});
    EXPECT_EQ(result.data, "local data");
}

#if NIX_WITH_SECRETSPEC

using nix::testing::SecretSpecFixture;

static constexpr std::string_view netrcManifest = R"(
[project]
name = "nix-filetransfer-test"
revision = "1.0"

[profiles.nix]
NIX_NETRC = { description = "netrc file", required = true, as_path = true }
)";

TEST(FileTransferSettings, resolvesSecretSpecNetrcFile)
{
    SecretSpecFixture fixture{netrcManifest, "NIX_NETRC=machine-example\n"};

    std::filesystem::path resolvedPath;
    {
        SecretSpecSettings secretSettings;
        fixture.configure(secretSettings);

        FileTransferSettings transferSettings{secretSettings};
        transferSettings.secretSpecNetrcFile = "NIX_NETRC";
        resolvedPath = transferSettings.getNetrcFile();

        EXPECT_EQ(readFile(resolvedPath), "machine-example");
        EXPECT_TRUE(pathExists(resolvedPath));
    }

    EXPECT_FALSE(pathExists(resolvedPath));
}

TEST(FileTransferSettings, rejectsInlineSecretSpecNetrcFile)
{
    SecretSpecFixture fixture{
        R"(
[project]
name = "nix-filetransfer-test"
revision = "1.0"

[profiles.nix]
NIX_NETRC = { description = "netrc file", required = true }
)",
        "NIX_NETRC=machine-example\n"};

    SecretSpecSettings secretSettings;
    fixture.configure(secretSettings);

    FileTransferSettings transferSettings{secretSettings};
    transferSettings.secretSpecNetrcFile = "NIX_NETRC";
    EXPECT_THROW(transferSettings.getNetrcFile(), Error);
}

#endif

} // namespace nix
