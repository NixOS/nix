#include <gtest/gtest.h>

#include "nix/store/uds-remote-store.hh"

namespace nix {

TEST(UDSRemoteStore, constructConfig)
{
    UDSRemoteStoreConfig config{"unix", "/tmp/socket", {}};

    EXPECT_EQ(config.path, "/tmp/socket");
}

TEST(UDSRemoteStore, constructConfigWrongScheme)
{
    EXPECT_THROW(UDSRemoteStoreConfig("http", "/tmp/socket", {}), UsageError);
}

TEST(UDSRemoteStore, constructConfig_to_string)
{
    UDSRemoteStoreConfig config{"unix", "", {}};
    EXPECT_EQ(config.getReference().to_string(), "daemon");
}

TEST(UDSRemoteStore, constructConfigWithParams)
{
    StoreConfig::Params params{{"max-connections", "1"}};
    UDSRemoteStoreConfig config{"unix", "/tmp/socket", params};
    auto storeReference = config.getReference();
    EXPECT_EQ(storeReference.to_string(), "unix:///tmp/socket?max-connections=1");
    EXPECT_EQ(storeReference.render(/*withParams=*/false), "unix:///tmp/socket");
    EXPECT_EQ(storeReference.params, params);
}

TEST(UDSRemoteStore, constructConfigWithParamsNoPath)
{
    StoreConfig::Params params{{"max-connections", "1"}};
    UDSRemoteStoreConfig config{"unix", "", params};
    auto storeReference = config.getReference();
    EXPECT_EQ(storeReference.to_string(), "daemon?max-connections=1");
    EXPECT_EQ(storeReference.render(/*withParams=*/false), "daemon");
    EXPECT_EQ(storeReference.params, params);
}

} // namespace nix
