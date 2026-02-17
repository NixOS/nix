#include <gtest/gtest.h>

#include "nix/store/globals.hh"
#include "nix/store/legacy-ssh-store.hh"
#include "nix/store/tests/test-main.hh"

namespace nix {

TEST(LegacySSHStore, constructConfig)
{
    auto settings = getTestSettings();
    LegacySSHStoreConfig config(
        settings,
        "ssh",
        "me@localhost:2222",
        StoreConfig::Params{
            {
                "remote-program",
                // TODO #11106, no more split on space
                "foo bar",
            },
        });

    EXPECT_EQ(
        config.remoteProgram.get(),
        (Strings{
            "foo",
            "bar",
        }));

    EXPECT_EQ(config.getReference().render(/*withParams=*/true), "ssh://me@localhost:2222?remote-program=foo%20bar");
    EXPECT_EQ(config.getReference().render(/*withParams=*/false), "ssh://me@localhost:2222");
}
} // namespace nix
