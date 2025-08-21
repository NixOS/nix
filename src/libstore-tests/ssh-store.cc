#include <gtest/gtest.h>

#include "nix/store/ssh-store.hh"

namespace nix {

TEST(SSHStore, constructConfig)
{
    SSHStoreConfig config{
        "ssh-ng",
        "me@localhost:2222",
        StoreConfig::Params{
            {
                "remote-program",
                {
                    "foo",
                    "bar",
                },
            },
        },
    };

    EXPECT_EQ(
        config.remoteProgram,
        (Strings{
            "foo",
            "bar",
        }));

    EXPECT_EQ(config.getReference().render(/*withParams=*/true), "ssh-ng://me@localhost:2222?remote-program=foo%20bar");
    config.authority.port = std::nullopt;
    EXPECT_EQ(config.getReference().render(/*withParams=*/true), "ssh-ng://me@localhost:2222");
}

TEST(MountedSSHStore, constructConfig)
{
    ExperimentalFeatureSettings mockXpSettings;
    mockXpSettings.set("experimental-features", "mounted-ssh-store");

    SSHStoreConfig config{
        "ssh-ng",
        "localhost",
        StoreConfig::Params{
            {
                "remote-program",
                {
                    "foo",
                    "bar",
                },
            },
            {
                "mounted",
                nlohmann::json::object_t{},
            },
        },
        mockXpSettings,
    };

    EXPECT_EQ(
        config.remoteProgram,
        (Strings{
            "foo",
            "bar",
        }));

    ASSERT_TRUE(config.mounted);

    EXPECT_EQ(config.mounted->realStoreDir, "/nix/store");
}

TEST(MountedSSHStore, constructConfigWithFunnyRealStoreDir)
{
    ExperimentalFeatureSettings mockXpSettings;
    mockXpSettings.set("experimental-features", "mounted-ssh-store");

    SSHStoreConfig config{
        "ssh-ng",
        "localhost",
        StoreConfig::Params{
            {
                "remote-program",
                {
                    "foo",
                    "bar",
                },
            },
            {
                "mounted",
                nlohmann::json::object_t{
                    {"real", "/foo/bar"},
                },
            },
        },
        mockXpSettings,
    };

    EXPECT_EQ(
        config.remoteProgram,
        (Strings{
            "foo",
            "bar",
        }));

    ASSERT_TRUE(config.mounted);

    EXPECT_EQ(config.mounted->realStoreDir, "/foo/bar");
}

} // namespace nix
