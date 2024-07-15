#include <gtest/gtest.h>

#include "uds-remote-store.hh"

namespace nix {

TEST(UDSRemoteStore, constructConfig)
{
    UDSRemoteStoreConfig config {
        "unix",
        "/tmp/socket",
        {}
    };

    EXPECT_EQ(*config.path, "/tmp/socket");
}

}