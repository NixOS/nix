#include <gtest/gtest.h>

#include "nix/store/uds-remote-store.hh"

namespace nix {

TEST(UDSRemoteStore, storeDir_absolutePath)
{
    std::filesystem::path storeDir =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    storeDir /= "nix";
    storeDir /= "store";
    UDSRemoteStoreConfig config{"", {{"store", storeDir.string()}}};
    EXPECT_EQ(config.storeDir, storeDir.string());
}

TEST(UDSRemoteStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(UDSRemoteStoreConfig("", {{"store", (std::filesystem::path{"nix"} / "store").string()}}), UsageError);
}

TEST(UDSRemoteStore, constructConfig)
{
    UDSRemoteStoreConfig config{"/tmp/socket", {}};

    EXPECT_EQ(config.path, "/tmp/socket");
}

TEST(UDSRemoteStore, constructConfig_to_string)
{
    UDSRemoteStoreConfig config{"", {}};
    EXPECT_EQ(config.getReference().to_string(), "daemon");
}

TEST(UDSRemoteStore, constructConfigWithParams)
{
    StoreConfig::Params params{{"max-connections", "1"}};
    UDSRemoteStoreConfig config{"/tmp/socket", params};
    auto storeReference = config.getReference();
    EXPECT_EQ(storeReference.to_string(), "unix:///tmp/socket?max-connections=1");
    EXPECT_EQ(storeReference.render(/*withParams=*/false), "unix:///tmp/socket");
    EXPECT_EQ(storeReference.params, params);
}

TEST(UDSRemoteStore, constructConfigWithParamsNoPath)
{
    StoreConfig::Params params{{"max-connections", "1"}};
    UDSRemoteStoreConfig config{"", params};
    auto storeReference = config.getReference();
    EXPECT_EQ(storeReference.to_string(), "daemon?max-connections=1");
    EXPECT_EQ(storeReference.render(/*withParams=*/false), "daemon");
    EXPECT_EQ(storeReference.params, params);
}

} // namespace nix
