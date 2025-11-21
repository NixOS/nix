#include <gtest/gtest.h>

#include "nix/store/globals.hh"
#include "nix/store/uds-remote-store.hh"

namespace nix {

TEST(UDSRemoteStore, constructConfig)
{
    Settings settings;
    UDSRemoteStoreConfig config{settings, "unix", "/tmp/socket", {}};

    EXPECT_EQ(config.path, "/tmp/socket");
}

TEST(UDSRemoteStore, constructConfigWrongScheme)
{
    Settings settings;
    EXPECT_THROW(UDSRemoteStoreConfig(settings, "http", "/tmp/socket", {}), UsageError);
}

TEST(UDSRemoteStore, constructConfig_to_string)
{
    Settings settings;
    UDSRemoteStoreConfig config{settings, "unix", "", {}};
    EXPECT_EQ(config.getReference().to_string(), "daemon");
}

TEST(UDSRemoteStore, constructConfigWithParams)
{
    Settings settings;

    StoreConfig::Params params{{"max-connections", "1"}};
    UDSRemoteStoreConfig config{settings, "unix", "/tmp/socket", params};

    auto storeReference = config.getReference();
    EXPECT_EQ(storeReference.to_string(), "unix:///tmp/socket?max-connections=1");
    EXPECT_EQ(storeReference.render(/*withParams=*/false), "unix:///tmp/socket");
    EXPECT_EQ(storeReference.params, params);
}

TEST(UDSRemoteStore, constructConfigWithParamsNoPath)
{
    Settings settings;

    StoreConfig::Params params{{"max-connections", "1"}};
    UDSRemoteStoreConfig config{settings, "unix", "", params};

    auto storeReference = config.getReference();
    EXPECT_EQ(storeReference.to_string(), "daemon?max-connections=1");
    EXPECT_EQ(storeReference.render(/*withParams=*/false), "daemon");
    EXPECT_EQ(storeReference.params, params);
}

} // namespace nix
