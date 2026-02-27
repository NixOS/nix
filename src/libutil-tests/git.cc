#include <gtest/gtest.h>

#include "nix/util/git.hh"
#include "nix/util/memory-source-accessor.hh"

#include "nix/util/tests/characterization.hh"

namespace nix {

using namespace git;

class GitTest : public CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "git";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / std::string(testStem);
    }

    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;

private:

    void SetUp() override
    {
        mockXpSettings.set("experimental-features", "git-hashing");
    }
};

TEST(GitMode, gitMode_directory)
{
    Mode m = Mode::Directory;
    RawMode r = 0040000;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST(GitMode, gitMode_executable)
{
    Mode m = Mode::Executable;
    RawMode r = 0100755;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST(GitMode, gitMode_regular)
{
    Mode m = Mode::Regular;
    RawMode r = 0100644;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST(GitMode, gitMode_symlink)
{
    Mode m = Mode::Symlink;
    RawMode r = 0120000;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST_F(GitTest, blob_read)
{
    readTest("hello-world-blob.bin", [&](const auto & encoded) {
        StringSource in{encoded};
        StringSink out;
        RegularFileSink out2{out};
        ASSERT_EQ(parseObjectType(in, mockXpSettings), ObjectType::Blob);
        parseBlob(out2, CanonPath::root, in, BlobMode::Regular, mockXpSettings);

        auto expected = readFile(goldenMaster("hello-world.bin"));

        ASSERT_EQ(out.s, expected);
    });
}

TEST_F(GitTest, blob_write)
{
    writeTest("hello-world-blob.bin", [&]() {
        auto decoded = readFile(goldenMaster("hello-world.bin"));
        StringSink s;
        dumpBlobPrefix(decoded.size(), s, mockXpSettings);
        s(decoded);
        return s.s;
    });
}

/**
 * This data is for "shallow" tree tests. However, we use "real" hashes
 * so that we can check our test data in a small shell script test test
 * (`src/libutil-tests/data/git/check-data.sh`).
 */
const static Tree treeSha1 = {
    {
        "Foo",
        {
            .mode = Mode::Regular,
            // hello world with special chars from above
            .hash = Hash::parseAny("63ddb340119baf8492d2da53af47e8c7cfcd5eb2", HashAlgorithm::SHA1),
        },
    },
    {
        "bAr",
        {
            .mode = Mode::Executable,
            // ditto
            .hash = Hash::parseAny("63ddb340119baf8492d2da53af47e8c7cfcd5eb2", HashAlgorithm::SHA1),
        },
    },
    {
        "baZ/",
        {
            .mode = Mode::Directory,
            // Empty directory hash
            .hash = Hash::parseAny("4b825dc642cb6eb9a060e54bf8d69288fbee4904", HashAlgorithm::SHA1),
        },
    },
    {
        "quuX",
        {
            .mode = Mode::Symlink,
            // hello world with special chars from above (symlink target
            // can be anything)
            .hash = Hash::parseAny("63ddb340119baf8492d2da53af47e8c7cfcd5eb2", HashAlgorithm::SHA1),
        },
    },
};

/**
 * Same conceptual object as `treeSha1`, just different hash algorithm.
 * See that one for details.
 */
const static Tree treeSha256 = {
    {
        "Foo",
        {
            .mode = Mode::Regular,
            .hash = Hash::parseAny(
                "ce60f5ad78a08ac24872ef74d78b078f077be212e7a246893a1a5d957dfbc8b1", HashAlgorithm::SHA256),
        },
    },
    {
        "bAr",
        {
            .mode = Mode::Executable,
            .hash = Hash::parseAny(
                "ce60f5ad78a08ac24872ef74d78b078f077be212e7a246893a1a5d957dfbc8b1", HashAlgorithm::SHA256),
        },
    },
    {
        "baZ/",
        {
            .mode = Mode::Directory,
            .hash = Hash::parseAny(
                "6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321", HashAlgorithm::SHA256),
        },
    },
    {
        "quuX",
        {
            .mode = Mode::Symlink,
            .hash = Hash::parseAny(
                "ce60f5ad78a08ac24872ef74d78b078f077be212e7a246893a1a5d957dfbc8b1", HashAlgorithm::SHA256),
        },
    },
};

static auto mkTreeReadTest(HashAlgorithm hashAlgo, Tree tree, const ExperimentalFeatureSettings & mockXpSettings)
{
    return [hashAlgo, tree, mockXpSettings](const auto & encoded) {
        StringSource in{encoded};
        NullFileSystemObjectSink out;
        Tree got;
        ASSERT_EQ(parseObjectType(in, mockXpSettings), ObjectType::Tree);
        parseTree(
            out,
            CanonPath::root,
            in,
            hashAlgo,
            [&](auto & name, auto entry) {
                auto name2 = std::string{name.rel()};
                if (entry.mode == Mode::Directory)
                    name2 += '/';
                got.insert_or_assign(name2, std::move(entry));
            },
            mockXpSettings);

        ASSERT_EQ(got, tree);
    };
}

TEST_F(GitTest, tree_sha1_read)
{
    readTest("tree-sha1.bin", mkTreeReadTest(HashAlgorithm::SHA1, treeSha1, mockXpSettings));
}

TEST_F(GitTest, tree_sha256_read)
{
    readTest("tree-sha256.bin", mkTreeReadTest(HashAlgorithm::SHA256, treeSha256, mockXpSettings));
}

TEST_F(GitTest, tree_sha1_write)
{
    writeTest("tree-sha1.bin", [&]() {
        StringSink s;
        dumpTree(treeSha1, s, mockXpSettings);
        return s.s;
    });
}

TEST_F(GitTest, tree_sha256_write)
{
    writeTest("tree-sha256.bin", [&]() {
        StringSink s;
        dumpTree(treeSha256, s, mockXpSettings);
        return s.s;
    });
}

namespace memory_source_accessor {

extern ref<MemorySourceAccessor> exampleComplex();

}

TEST_F(GitTest, both_roundrip)
{
    auto files = memory_source_accessor::exampleComplex();

    for (const auto hashAlgo : {HashAlgorithm::SHA1, HashAlgorithm::SHA256}) {
        std::map<Hash, std::string> cas;

        fun<DumpHook> dumpHook = [&](const SourcePath & path) {
            StringSink s;
            HashSink hashSink{hashAlgo};
            TeeSink s2{s, hashSink};
            auto mode = dump(path, s2, dumpHook, defaultPathFilter, mockXpSettings);
            auto hash = hashSink.finish().hash;
            cas.insert_or_assign(hash, std::move(s.s));
            return TreeEntry{
                .mode = mode,
                .hash = hash,
            };
        };

        auto root = dumpHook(SourcePath{files});

        auto files2 = make_ref<MemorySourceAccessor>();

        MemorySink sinkFiles2{*files2};

        std::function<void(const CanonPath, const Hash &, BlobMode)> mkSinkHook;
        mkSinkHook = [&](auto prefix, auto & hash, auto blobMode) {
            StringSource in{cas[hash]};
            parse(
                sinkFiles2,
                prefix,
                in,
                blobMode,
                hashAlgo,
                [&](const CanonPath & name, const auto & entry) {
                    mkSinkHook(
                        prefix / name,
                        entry.hash,
                        // N.B. this cast would not be acceptable in real
                        // code, because it would make an assert reachable,
                        // but it should harmless in this test.
                        static_cast<BlobMode>(entry.mode));
                },
                mockXpSettings);
        };

        mkSinkHook(CanonPath::root, root.hash, BlobMode::Regular);

        EXPECT_EQ(files->root, files2->root);
    }
}

TEST(GitLsRemote, parseSymrefLineWithReference)
{
    auto line = "ref: refs/head/main	HEAD";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, "HEAD");
}

TEST(GitLsRemote, parseSymrefLineWithNoReference)
{
    auto line = "ref: refs/head/main";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, std::nullopt);
}

TEST(GitLsRemote, parseObjectRefLine)
{
    auto line = "abc123	refs/head/main";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Object);
    ASSERT_EQ(res->target, "abc123");
    ASSERT_EQ(res->reference, "refs/head/main");
}

} // namespace nix
