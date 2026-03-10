#include <gtest/gtest.h>

#include "nix/store/legacy-ssh-store.hh"

namespace nix {

TEST(LegacySSHStore, storeDir_absolutePath)
{
    LegacySSHStoreConfig config{
        ParsedURL::Authority::parse("localhost"),
        {{"store", "/my/store"}},
    };
    EXPECT_EQ(config.storeDir, "/my/store");
}

TEST(LegacySSHStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(LegacySSHStoreConfig(ParsedURL::Authority::parse("localhost"), {{"store", "my/store"}}), UsageError);
}

TEST(LegacySSHStore, constructConfig)
{
    LegacySSHStoreConfig config(
        ParsedURL::Authority::parse("me@localhost:2222"),
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
