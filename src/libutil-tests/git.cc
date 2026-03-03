#include <gtest/gtest.h>

#include "nix/util/git.hh"
#include "nix/util/memory-source-accessor.hh"

#include "nix/util/tests/characterization.hh"

namespace nix {

/**
 * Test implementation of merkle::DirectorySink that captures entries.
 */
struct TestDirectorySink : merkle::DirectorySink
{
    git::Tree entries;

    void insertChild(std::string_view name, merkle::TreeEntry entry) override
    {
        auto name2 = std::string{name};
        if (entry.mode == merkle::Mode::Directory)
            name2 += '/';
        entries.insert_or_assign(name2, std::move(entry));
    }
};

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
    using namespace git;
    Mode m = Mode::Directory;
    RawMode r = 0040000;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST(GitMode, gitMode_executable)
{
    using namespace git;
    Mode m = Mode::Executable;
    RawMode r = 0100755;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST(GitMode, gitMode_regular)
{
    using namespace git;
    Mode m = Mode::Regular;
    RawMode r = 0100644;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST(GitMode, gitMode_symlink)
{
    using namespace git;
    Mode m = Mode::Symlink;
    RawMode r = 0120000;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional{m});
};

TEST_F(GitTest, blob_read)
{
    using namespace git;
    readTest("hello-world-blob.bin", [&](const auto & encoded) {
        StringSource in{encoded};
        StringSink out;
        ASSERT_EQ(parseObjectType(in, mockXpSettings), ObjectType::Blob);
        auto size = parseBlob(in, mockXpSettings);
        in.drainInto(out, size);

        auto expected = readFile(goldenMaster("hello-world.bin"));

        ASSERT_EQ(out.s, expected);
    });
}

TEST_F(GitTest, blob_read_large_size)
{
    // Test that parseBlob handles sizes larger than INT_MAX (2^31 - 1)
    // This verifies that a bad overflowing number parser isn't used.
    uint64_t largeSize = 5000000000ULL; // ~5 GB, larger than INT_MAX
    std::string blobHeader = std::to_string(largeSize);
    blobHeader.push_back('\0'); // null terminator expected by parseBlob

    StringSource in{blobHeader};
    auto size = git::parseBlob(in, mockXpSettings);

    ASSERT_EQ(size, largeSize);
}

TEST_F(GitTest, blob_write)
{
    using namespace git;
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
const static git::Tree treeSha1 = {
    {
        "Foo",
        {
            .mode = git::Mode::Regular,
            // hello world with special chars from above
            .hash = Hash::parseAny("63ddb340119baf8492d2da53af47e8c7cfcd5eb2", HashAlgorithm::SHA1),
        },
    },
    {
        "bAr",
        {
            .mode = git::Mode::Executable,
            // ditto
            .hash = Hash::parseAny("63ddb340119baf8492d2da53af47e8c7cfcd5eb2", HashAlgorithm::SHA1),
        },
    },
    {
        "baZ/",
        {
            .mode = git::Mode::Directory,
            // Empty directory hash
            .hash = Hash::parseAny("4b825dc642cb6eb9a060e54bf8d69288fbee4904", HashAlgorithm::SHA1),
        },
    },
    {
        "quuX",
        {
            .mode = git::Mode::Symlink,
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
const static git::Tree treeSha256 = {
    {
        "Foo",
        {
            .mode = git::Mode::Regular,
            .hash = Hash::parseAny(
                "ce60f5ad78a08ac24872ef74d78b078f077be212e7a246893a1a5d957dfbc8b1", HashAlgorithm::SHA256),
        },
    },
    {
        "bAr",
        {
            .mode = git::Mode::Executable,
            .hash = Hash::parseAny(
                "ce60f5ad78a08ac24872ef74d78b078f077be212e7a246893a1a5d957dfbc8b1", HashAlgorithm::SHA256),
        },
    },
    {
        "baZ/",
        {
            .mode = git::Mode::Directory,
            .hash = Hash::parseAny(
                "6ef19b41225c5369f1c104d45d8d85efa9b057b53b14b4b9b939dd74decc5321", HashAlgorithm::SHA256),
        },
    },
    {
        "quuX",
        {
            .mode = git::Mode::Symlink,
            .hash = Hash::parseAny(
                "ce60f5ad78a08ac24872ef74d78b078f077be212e7a246893a1a5d957dfbc8b1", HashAlgorithm::SHA256),
        },
    },
};

static auto mkTreeReadTest(HashAlgorithm hashAlgo, git::Tree tree, const ExperimentalFeatureSettings & mockXpSettings)
{
    using namespace git;
    return [hashAlgo, tree, mockXpSettings](const auto & encoded) {
        StringSource in{encoded};
        TestDirectorySink out;
        ASSERT_EQ(parseObjectType(in, mockXpSettings), ObjectType::Tree);
        parseTree(out, in, hashAlgo, mockXpSettings);

        ASSERT_EQ(out.entries, tree);
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
    using namespace git;
    writeTest("tree-sha1.bin", [&]() {
        StringSink s;
        dumpTree(treeSha1, s, mockXpSettings);
        return s.s;
    });
}

TEST_F(GitTest, tree_sha256_write)
{
    using namespace git;
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
    using namespace git;
    auto files = memory_source_accessor::exampleComplex();

    for (const auto hashAlgo : {HashAlgorithm::SHA1, HashAlgorithm::SHA256}) {
        std::map<Hash, std::string> cas;

        // Dump phase: serialize files to git objects in cas
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

        // Parse phase: deserialize git objects back to files
        auto files2 = make_ref<MemorySourceAccessor>();

        // Recursive function to parse a git object into a File
        std::function<MemorySourceAccessor::File(merkle::TreeEntry)> parseToFile;

        // DirectorySink that recursively parses children
        struct RecursiveDirSink : merkle::DirectorySink
        {
            std::function<MemorySourceAccessor::File(merkle::TreeEntry)> & parseToFile;
            MemorySourceAccessor::File::Directory dir;

            RecursiveDirSink(std::function<MemorySourceAccessor::File(merkle::TreeEntry)> & parseToFile)
                : parseToFile(parseToFile)
            {
            }

            void insertChild(std::string_view name, merkle::TreeEntry entry) override
            {
                dir.entries.insert_or_assign(std::string{name}, parseToFile(entry));
            }
        };

        parseToFile = [&](merkle::TreeEntry entry) -> MemorySourceAccessor::File {
            StringSource in{cas[entry.hash]};
            auto type = parseObjectType(in, mockXpSettings);

            switch (type) {
            case ObjectType::Blob: {
                StringSink content;
                auto size = parseBlob(in, mockXpSettings);
                in.drainInto(content, size);
                if (entry.mode == merkle::Mode::Symlink) {
                    return MemorySourceAccessor::File::Symlink{std::move(content.s)};
                } else {
                    return MemorySourceAccessor::File::Regular{
                        .executable = entry.mode == merkle::Mode::Executable,
                        .contents = std::move(content.s),
                    };
                }
            }
            case ObjectType::Tree: {
                RecursiveDirSink dirSink{parseToFile};
                parseTree(dirSink, in, hashAlgo, mockXpSettings);
                return std::move(dirSink.dir);
            }
            default:
                assert(false);
            }
        };

        files2->root = parseToFile(root);

        EXPECT_EQ(files->root, files2->root);
    }
}

TEST(GitLsRemote, parseSymrefLineWithReference)
{
    using namespace git;
    auto line = "ref: refs/head/main	HEAD";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, "HEAD");
}

TEST(GitLsRemote, parseSymrefLineWithNoReference)
{
    using namespace git;
    auto line = "ref: refs/head/main";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, std::nullopt);
}

TEST(GitLsRemote, parseObjectRefLine)
{
    using namespace git;
    auto line = "abc123	refs/head/main";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Object);
    ASSERT_EQ(res->target, "abc123");
    ASSERT_EQ(res->reference, "refs/head/main");
}

} // namespace nix
