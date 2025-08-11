#include <gtest/gtest.h>

#include "nix/store/legacy-ssh-store.hh"

namespace nix {

TEST(LegacySSHStore, constructConfig)
{
    initLibStore(/*loadConfig=*/false);

    auto config = make_ref<LegacySSHStoreConfig>(
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
        config->remoteProgram.get(),
        (Strings{
            "foo",
            "bar",
        }));

    auto store = config->openStore();
    EXPECT_EQ(store->getUri(), "ssh://me@localhost:2222?remote-program=foo%20bar");
}
} // namespace nix
