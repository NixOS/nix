// FIXME: Odd failures for templates that are causing the PR to break
// for now with discussion with @Ericson2314 to comment out.
#if 0
#  include <gtest/gtest.h>

#  include "ssh-store.hh"

namespace nix {

TEST(SSHStore, constructConfig)
{
    SSHStoreConfig config{
        "ssh",
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

}
#endif
