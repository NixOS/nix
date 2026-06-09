#include <gtest/gtest.h>

#include "nix/store/local-fs-store.hh"
#include "nix/store/globals.hh"

namespace nix {

namespace {

/**
 * Concrete subclass of `LocalFSStoreConfig` for testing, since
 * `LocalFSStoreConfig` is abstract (`openStore()` is pure virtual).
 */
struct TestLocalFSStoreConfig : LocalFSStoreConfig
{
    TestLocalFSStoreConfig(const Params & params)
        : StoreConfig(params, FilePathType::Native)
        , LocalFSStoreConfig(params)
    {
    }

    TestLocalFSStoreConfig(const std::filesystem::path & path, const Params & params)
        : StoreConfig(params, FilePathType::Native)
        , LocalFSStoreConfig(path, params)
    {
    }

    ref<Store> openStore() const override
    {
        unreachable();
    }
};

} // namespace

TEST(LocalFSStoreConfig, getStateDir_default)
{
    TestLocalFSStoreConfig config{{}};
    // Default stateDir should be the global settings value
    EXPECT_EQ(config.getStateDir(), settings.nixStateDir);
}

TEST(LocalFSStoreConfig, getStateDir_withRoot)
{
    std::filesystem::path root =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    root /= "foo";
    root /= "bar";
    auto expectedStateDir = root / "nix" / "var" / "nix";
    TestLocalFSStoreConfig config{root, {}};
    EXPECT_EQ(config.getStateDir(), expectedStateDir);
}

TEST(LocalFSStoreConfig, getStateDir_explicitSetting)
{
    std::filesystem::path stateDir =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    stateDir /= "custom";
    stateDir /= "state";
    TestLocalFSStoreConfig config{{{"state", stateDir.string()}}};
    EXPECT_EQ(config.getStateDir(), stateDir);
}

} // namespace nix
