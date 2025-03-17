#include <gtest/gtest.h>

#include "dummy-store.hh"
#include "globals.hh"

namespace nix {

TEST(DummyStore, constructConfig)
{
    DummyStoreConfig config{"dummy", "", {}};

    EXPECT_EQ(config.storeDir, settings.nixStore);
}

TEST(DummyStore, constructConfigNoAuthority)
{
    EXPECT_THROW(DummyStoreConfig("dummy", "not-allowed", {}), UsageError);
}

} // namespace nix
