#include <gtest/gtest.h>

#include "nix/store/ssh-store.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

namespace nix {

TEST(SSHStore, storeDir_absolutePath)
{
    SSHStoreConfig config{
        ParsedURL::Authority::parse("localhost"),
        {{"store", "/my/store"}},
    };
    EXPECT_EQ(config.storeDir, "/my/store");
}

TEST(SSHStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(SSHStoreConfig(ParsedURL::Authority::parse("localhost"), {{"store", "my/store"}}), UsageError);
}

TEST(SSHStore, constructConfig)
{
    SSHStoreConfig config{
        ParsedURL::Authority::parse("me@localhost:2222"),
        StoreConfig::Params{
            {
                "remote-program",
                // TODO #11106, no more split on space
                "foo bar",
            },
        },
    };

    EXPECT_EQ(
        config.remoteProgram.get(),
        (Strings{
            "foo",
            "bar",
        }));

    EXPECT_EQ(config.getReference().render(/*withParams=*/true), "ssh-ng://me@localhost:2222?remote-program=foo%20bar");
    config.resetOverridden();
    EXPECT_EQ(config.getReference().render(/*withParams=*/true), "ssh-ng://me@localhost:2222");
}

TEST(MountedSSHStore, storeDir_absolutePath)
{
    std::filesystem::path storeDir =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    storeDir /= "nix";
    storeDir /= "store";
    MountedSSHStoreConfig config{{.host = "localhost"}, {{"store", storeDir.string()}}};
    EXPECT_EQ(config.storeDir, storeDir.string());
}

TEST(MountedSSHStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(
        MountedSSHStoreConfig({.host = "localhost"}, {{"store", (std::filesystem::path{"nix"} / "store").string()}}),
        UsageError);
}

TEST(MountedSSHStore, constructConfig)
{
    MountedSSHStoreConfig config{
        {.host = "localhost"},
        StoreConfig::Params{
            {
                "remote-program",
                // TODO #11106, no more split on space
                "foo bar",
            },
        },
    };

    EXPECT_EQ(
        config.remoteProgram.get(),
        (Strings{
            "foo",
            "bar",
        }));
}

} // namespace nix
