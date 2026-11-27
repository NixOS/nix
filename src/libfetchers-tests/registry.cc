#include "nix/fetchers/fetch-settings.hh"
#include "nix/fetchers/registry.hh"
#include "nix/store/tests/libstore.hh"
#include "nix/util/file-system.hh"
#include "nix/util/tests/capture-logging.hh"

#include <gtest/gtest.h>

namespace nix::fetchers {

TEST(Registry, customRegistriesUseRequestedPath)
{
    EnableExperimentalFeature enableFlakes("flakes");
    Settings settings;
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);

    auto firstInput = Input::fromAttrs({{"type", "indirect"}, {"id", "first"}});
    auto secondInput = Input::fromAttrs({{"type", "indirect"}, {"id", "second"}});
    auto target = Input::fromAttrs({{"type", "indirect"}, {"id", "target"}});
    auto firstPath = tmpDir / "first.json";
    auto secondPath = tmpDir / "second.json";

    Registry first(Registry::Custom);
    first.add(firstInput, target, {});
    first.write(firstPath);

    Registry second(Registry::Custom);
    second.add(secondInput, target, {});
    second.write(secondPath);

    auto firstRegistry = getCustomRegistry(settings, firstPath);
    ASSERT_EQ(firstRegistry->entries.size(), 1u);
    EXPECT_EQ(firstRegistry->entries[0].from, firstInput);
    EXPECT_EQ(firstRegistry->entries[0].to, target);

    auto secondRegistry = getCustomRegistry(settings, secondPath);
    ASSERT_EQ(secondRegistry->entries.size(), 1u);
    EXPECT_EQ(secondRegistry->entries[0].from, secondInput);
    EXPECT_EQ(secondRegistry->entries[0].to, target);
    EXPECT_EQ(firstRegistry->entries[0].from, firstInput);
}

TEST(Registry, missingCustomRegistryIsEmpty)
{
    Settings settings;
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    nix::testing::CaptureLogging log;

    auto registry = getCustomRegistry(settings, tmpDir / "missing.json");

    EXPECT_EQ(registry->type, Registry::Custom);
    EXPECT_TRUE(registry->entries.empty());
    EXPECT_EQ(log.get(), "");
}

TEST(Registry, customRegistryParseErrorReportsPath)
{
    Settings settings;
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    auto path = tmpDir / "invalid.json";
    writeFile(path, "{");
    nix::testing::CaptureLogging log;

    auto registry = getCustomRegistry(settings, path);

    EXPECT_TRUE(registry->entries.empty());
    EXPECT_THAT(log.get(), ::testing::HasSubstr("cannot parse flake registry '" + path.generic_string() + "'"));
}

TEST(Registry, customRegistryReadErrorReportsPath)
{
    Settings settings;
    auto tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir, true);
    nix::testing::CaptureLogging log;

    auto registry = getCustomRegistry(settings, tmpDir);

    EXPECT_TRUE(registry->entries.empty());
    EXPECT_THAT(log.get(), ::testing::HasSubstr("cannot read flake registry '" + tmpDir.string() + "'"));
}

} // namespace nix::fetchers
