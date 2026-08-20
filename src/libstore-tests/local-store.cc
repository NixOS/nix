#include <gtest/gtest.h>

#include "nix/store/local-store.hh"
#include "nix/store/globals.hh"

#include "nix/util/archive.hh"
#include "nix/util/memory-source-accessor.hh"
// Needed for template specialisations. This is not good! When we
// overhaul how store configs work, this should be fixed.
#include "nix/util/args.hh"
#include "nix/util/config-impl.hh"
#include "nix/util/abstract-setting-to-json.hh"

namespace nix {

TEST(LocalStore, storeDir_absolutePath)
{
    std::filesystem::path storeDir =
#ifdef _WIN32
        "C:\\";
#else
        "/";
#endif
    storeDir /= "nix";
    storeDir /= "store";
    LocalStoreConfig config{"", {{"store", storeDir.string()}}};
    EXPECT_EQ(config.storeDir, storeDir.string());
}

TEST(LocalStore, storeDir_relativePath_rejected)
{
    EXPECT_THROW(LocalStoreConfig("", {{"store", (std::filesystem::path{"nix"} / "store").string()}}), UsageError);
}

TEST(LocalStore, storeDir_empty_rejected)
{
    EXPECT_THROW(LocalStoreConfig("", {{"store", ""}}), UsageError);
}

TEST(LocalStore, constructConfig_rootQueryParam)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalStoreConfig config{
        "",
        {
            {
                "root",
                std::string{root},
            },
        },
    };

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{std::string{root}});
}

TEST(LocalStore, constructConfig_rootPath)
{
#ifdef _WIN32
    constexpr std::string_view root = "C:\\foo\\bar";
#else
    constexpr std::string_view root = "/foo/bar";
#endif
    LocalStoreConfig config{std::string{root}, {}};

    EXPECT_EQ(config.rootDir.get(), std::optional<AbsolutePath>{std::string{root}});
}

TEST(LocalStore, constructConfig_to_string)
{
    LocalStoreConfig config{"", {}};
    EXPECT_EQ(config.getReference().to_string(), "local");
}

#ifndef _WIN32 /* Windows doesn't exactly have a notion of canonicalation for now. */

class LocalStoreCanonicalisationTest : public ::testing::TestWithParam<std::tuple<HashAlgorithm>>
{
protected:
    AutoDelete tempStoreDir;
    std::shared_ptr<LocalStoreConfig> config;
    std::shared_ptr<LocalStore> store; // Inexplicably, ref doesn't have a .release() method.

    void SetUp() override
    {
        tempStoreDir = canonPath(createTempDir(), /*resolveSymlinks=*/true);
        config = std::make_shared<LocalStoreConfig>(tempStoreDir.path(), StoreConfig::Params{});
        store = std::make_shared<LocalStore>(ref{config});
    }

    void TearDown() override
    {
        tempStoreDir.deletePath();
        store.reset();
    }

    void assertCanonicalPermissions(const std::filesystem::path & path)
    {
        auto st = lstat(path);

        /* TODO: Figure out how to test xattrs properly. Maybe with posix ACLs?
           But xattrs are unavailable in the nix sandbox, so that would have to
           also run in NixOS tests? */

        auto modeTypeMaskedOut = st.st_mode & ~S_IFMT;

        /* Store mtime is always 1 sec into the epoch. */
        ASSERT_EQ(st.st_mtime, 1);

        if (S_ISLNK(st.st_mode)) {
            /* Not much to check for symlinks. */
        } else if (S_ISREG(st.st_mode)) {
            ASSERT_TRUE(modeTypeMaskedOut == 0555 || modeTypeMaskedOut == 0444);
        } else if (S_ISDIR(st.st_mode)) {
            ASSERT_EQ(modeTypeMaskedOut, 0555);
            for (const auto & p : DirectoryIterator{path})
                assertCanonicalPermissions(p.path());
        } else {
            FAIL() << fmt("unknown file type at %1%", PathFmt(path));
        }
    }

    HashAlgorithm getHashAlgo() const
    {
        return std::get<HashAlgorithm>(GetParam());
    }
};

TEST_P(LocalStoreCanonicalisationTest, flatFitsInMemory)
{
    using namespace std::string_view_literals;
    StringSource source{"very simple file"sv};
    auto sp = store->addToStoreFromDump(
        source,
        "flat",
        FileSerialisationMethod::Flat,
        /* For the purposes of this test, it doesn't really matter to vary the hashing method. */
        ContentAddressMethod::Raw::NixArchive,
        getHashAlgo(),
        {},
        NoRepair);
    assertCanonicalPermissions(store->toRealPath(sp));
    StringSink narDump;
    dumpString(source.s, narDump);
    StringSource narSource{narDump.s};
    auto sp2 = store->addToStoreFromDump(
        narSource,
        "nar",
        FileSerialisationMethod::NixArchive,
        /* For the purposes of this test, it doesn't really matter to vary the hashing method. */
        ContentAddressMethod::Raw::NixArchive,
        getHashAlgo(),
        {},
        NoRepair);
    assertCanonicalPermissions(store->toRealPath(sp2));
}

TEST_P(LocalStoreCanonicalisationTest, simpleNar)
{
    auto accessor = make_ref<MemorySourceAccessor>();
    MemorySink memorySink{*accessor};
    memorySink.createDirectory(CanonPath::root);
    memorySink.createRegularFile(CanonPath("/regular"), [](auto & crf) { crf("test"); });
    memorySink.createRegularFile(CanonPath("/executable"), [](auto & crf) {
        crf("test");
        crf.isExecutable();
    });
    memorySink.createDirectory(CanonPath("/dir"));
    memorySink.createRegularFile(CanonPath("/dir/regular"), [](auto & crf) { crf("test 2"); });
    memorySink.createRegularFile(CanonPath("/dir/executable"), [](auto & crf) {
        crf("test 2");
        crf.isExecutable();
    });
    memorySink.createSymlink(CanonPath("/symlink"), "some target");
    memorySink.createSymlink(CanonPath("/dir/symlink"), "..");
    auto source = sinkToSource([&accessor](Sink & sink) { accessor->dumpPath(CanonPath::root, sink); });
    auto sp = store->addToStoreFromDump(
        *source,
        "nar-from-dump",
        FileSerialisationMethod::NixArchive,
        /* For the purposes of this test, it doesn't really matter to vary the hashing method. */
        ContentAddressMethod::Raw::NixArchive,
        getHashAlgo(),
        {},
        NoRepair);
    assertCanonicalPermissions(store->toRealPath(sp));

    memorySink.createRegularFile(CanonPath("/other"), [](auto & crf) { crf(""); });
    auto sp2 = store->addToStoreSlow("nar-slow", {accessor}, ContentAddressMethod::Raw::NixArchive, getHashAlgo()).path;
    assertCanonicalPermissions(store->toRealPath(sp2));

    Finally restoreNarBufferSize{[oldSize = settings.getLocalSettings().narBufferSize]() {
        settings.getLocalSettings().narBufferSize.assign(oldSize);
    }};
    // So that we always spill to the file system.
    settings.getLocalSettings().narBufferSize = 0;

    memorySink.createDirectory(CanonPath("/dir/entirely-something-else"));
    StringSink narDump;
    accessor->dumpPath(CanonPath::root, narDump);
    StringSource narSource{narDump.s};
    auto sp3 = store->addToStoreFromDump(
        narSource,
        "nar-from-dump-spilled",
        FileSerialisationMethod::NixArchive,
        /* For the purposes of this test, it doesn't really matter to vary the hashing method. */
        ContentAddressMethod::Raw::NixArchive,
        getHashAlgo(),
        {},
        NoRepair);
    assertCanonicalPermissions(store->toRealPath(sp3));
}

INSTANTIATE_TEST_SUITE_P(
    LocalStoreCanonicalisation,
    LocalStoreCanonicalisationTest,
    ::testing::Combine(
        ::testing::Values(
            HashAlgorithm::SHA256,
            /* Other algorithms are included because SHA256 is special-cased in some places,
               to avoid computing narHash twice. */
            HashAlgorithm::SHA1,
            HashAlgorithm::SHA512,
            HashAlgorithm::MD5)),
    [](const ::testing::TestParamInfo<LocalStoreCanonicalisationTest::ParamType> & info) {
        return std::string(printHashAlgo(std::get<HashAlgorithm>(info.param)));
    });

#endif

} // namespace nix
