#include <gtest/gtest.h>

#include "ssh-store.hh"

namespace nix {

TEST(SSHStore, constructConfig)
{
    SSHStoreConfig config{
        "ssh-ng",
        "localhost",
        StoreReference::Params{
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
        config.remoteProgram.get(),
        (Strings{
            "foo",
            "bar",
        }));
}

TEST(MountedSSHStore, constructConfig)
{
    SSHStoreConfig config{
        "ssh-ng",
        "localhost",
        StoreReference::Params{
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
    };

    EXPECT_EQ(
        config.remoteProgram.get(),
        (Strings{
            "foo",
            "bar",
        }));

    ASSERT_TRUE(config.mounted);

    EXPECT_EQ(config.mounted->realStoreDir, "/nix/store");
}

}
