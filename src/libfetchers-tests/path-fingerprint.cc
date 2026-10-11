#include "nix/fetchers/fetchers.hh"
#include "nix/fetchers/fetch-settings.hh"
#include "nix/store/dummy-store-impl.hh"
#include "nix/store/globals.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/configuration.hh"
#include "nix/util/memory-source-accessor.hh"

#include <gtest/gtest.h>

namespace nix {

class PathFingerprintTest : public LibStoreTest
{
protected:
    fetchers::Settings fetchSettings;

    PathFingerprintTest()
        : LibStoreTest([] {
            auto config = make_ref<DummyStoreConfig>(DummyStoreConfig::Params{});
            config->readOnly = false;
            return config->openDummyStore();
        }())
    {
    }

    void SetUp() override
    {
        LibStoreTest::SetUp();
        experimentalFeatureSettings.experimentalFeatures.get().insert(Xp::Flakes);
    }
};

TEST_F(PathFingerprintTest, valid_store_path_returns_fingerprint)
{
    auto storePath = store->addToStore(
        "source",
        SourcePath{
            [] {
                auto sc = make_ref<MemorySourceAccessor>();
                sc->root = MemorySourceAccessor::File{MemorySourceAccessor::File::Regular{
                    .executable = false,
                    .contents = "test content",
                }};
                return sc;
            }(),
        },
        ContentAddressMethod::Raw::NixArchive,
        HashAlgorithm::SHA256);

    auto input = fetchers::Input::fromAttrs(
        fetchSettings, fetchers::Attrs{{"type", "path"}, {"path", store->printStorePath(storePath)}});

    auto fingerprint = input.getFingerprint(*store);
    ASSERT_TRUE(fingerprint.has_value());
    EXPECT_TRUE(fingerprint->starts_with("path:"));
}

TEST_F(PathFingerprintTest, relative_path_returns_nullopt)
{
    auto input =
        fetchers::Input::fromAttrs(fetchSettings, fetchers::Attrs{{"type", "path"}, {"path", "./relative/path"}});

    EXPECT_NO_THROW({
        auto fingerprint = input.getFingerprint(*store);
        EXPECT_FALSE(fingerprint.has_value());
    });
}

TEST_F(PathFingerprintTest, non_store_path_returns_nullopt)
{
    auto input = fetchers::Input::fromAttrs(
        fetchSettings, fetchers::Attrs{{"type", "path"}, {"path", "/nonexistent/store/path"}});

    EXPECT_NO_THROW({
        auto fingerprint = input.getFingerprint(*store);
        EXPECT_FALSE(fingerprint.has_value());
    });
}

} // namespace nix
