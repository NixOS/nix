#include <gtest/gtest.h>

#include "nix/store/legacy-ssh-store.hh"

namespace nix {

TEST(LegacySSHStore, constructConfig)
{
    LegacySSHStoreConfig config(
        "ssh",
        "me@localhost:2222",
        StoreConfig::Params{
            {
                "remote-program",
                {
                    "foo",
                    "bar",
                },
            },
        });

    EXPECT_EQ(
        config.remoteProgram,
        (Strings{
            "foo",
            "bar",
        }));

    EXPECT_EQ(config.getReference().render(/*withParams=*/true), "ssh://me@localhost:2222?remote-program=foo%20bar");
    EXPECT_EQ(config.getReference().render(/*withParams=*/false), "ssh://me@localhost:2222");
}
} // namespace nix
