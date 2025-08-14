#include <gtest/gtest.h>

#include "nix/store/ssh-store.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

namespace nix {

TEST(SSHStore, constructConfig)
{
    SSHStoreConfig config{
        "ssh-ng",
        "me@localhost:2222",
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

TEST(MountedSSHStore, constructConfig)
{
    MountedSSHStoreConfig config{
        "mounted-ssh",
        "localhost",
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
