#include <gtest/gtest.h>

#include "git.hh"
#include "memory-source-accessor.hh"

#include "tests/characterization.hh"

namespace nix {

using namespace git;

class GitTest : public CharacterizationTest
{
    Path unitTestData = getUnitTestData() + "/git";

public:

    Path goldenMaster(std::string_view testStem) const override {
        return unitTestData + "/" + testStem;
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

TEST(GitMode, gitMode_directory) {
    Mode m = Mode::Directory;
    RawMode r = 0040000;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional { m });
};

TEST(GitMode, gitMode_executable) {
    Mode m = Mode::Executable;
    RawMode r = 0100755;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional { m });
};

TEST(GitMode, gitMode_regular) {
    Mode m = Mode::Regular;
    RawMode r = 0100644;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional { m });
};

TEST(GitMode, gitMode_symlink) {
    Mode m = Mode::Symlink;
    RawMode r = 0120000;
    ASSERT_EQ(static_cast<RawMode>(m), r);
    ASSERT_EQ(decodeMode(r), std::optional { m });
};

TEST_F(GitTest, blob_read) {
    readTest("hello-world-blob.bin", [&](const auto & encoded) {
        StringSource in { encoded };
        StringSink out;
        RegularFileSink out2 { out };
        ASSERT_EQ(parseObjectType(in, mockXpSettings), ObjectType::Blob);
        parseBlob(out2, "", in, BlobMode::Regular, mockXpSettings);

        auto expected = readFile(goldenMaster("hello-world.bin"));

        ASSERT_EQ(out.s, expected);
    });
}

TEST_F(GitTest, blob_write) {
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
 * (`tests/unit/libutil/data/git/check-data.sh`).
 */
const static Tree tree = {
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

TEST_F(GitTest, tree_read) {
    readTest("tree.bin", [&](const auto & encoded) {
        StringSource in { encoded };
        NullFileSystemObjectSink out;
        Tree got;
        ASSERT_EQ(parseObjectType(in, mockXpSettings), ObjectType::Tree);
        parseTree(out, "", in, [&](auto & name, auto entry) {
            auto name2 = name;
            if (entry.mode == Mode::Directory)
                name2 += '/';
            got.insert_or_assign(name2, std::move(entry));
        }, mockXpSettings);

        ASSERT_EQ(got, tree);
    });
}

TEST_F(GitTest, tree_write) {
    writeTest("tree.bin", [&]() {
        StringSink s;
        dumpTree(tree, s, mockXpSettings);
        return s.s;
    });
}

TEST_F(GitTest, both_roundrip) {
    using File = MemorySourceAccessor::File;

    MemorySourceAccessor files;
    files.root = File::Directory {
        .contents {
            {
                "foo",
                File::Regular {
                    .contents = "hello\n\0\n\tworld!",
                },
            },
            {
                "bar",
                File::Directory {
                    .contents = {
                        {
                            "baz",
                            File::Regular {
                                .executable = true,
                                .contents = "good day,\n\0\n\tworld!",
                            },
                        },
                        {
                            "quux",
                            File::Symlink {
                                .target = "/over/there",
                            },
                        },
                    },
                },
            },
        },
    };

    std::map<Hash, std::string> cas;

    std::function<DumpHook> dumpHook;
    dumpHook = [&](const CanonPath & path) {
        StringSink s;
        HashSink hashSink { HashAlgorithm::SHA1 };
        TeeSink s2 { s, hashSink };
        auto mode = dump(
            files, path, s2, dumpHook,
            defaultPathFilter, mockXpSettings);
        auto hash = hashSink.finish().first;
        cas.insert_or_assign(hash, std::move(s.s));
        return TreeEntry {
            .mode = mode,
            .hash = hash,
        };
    };

    auto root = dumpHook(CanonPath::root);

    MemorySourceAccessor files2;

    MemorySink sinkFiles2 { files2 };

    std::function<void(const Path, const Hash &, BlobMode)> mkSinkHook;
    mkSinkHook = [&](auto prefix, auto & hash, auto blobMode) {
        StringSource in { cas[hash] };
        parse(
            sinkFiles2, prefix, in, blobMode,
            [&](const Path & name, const auto & entry) {
                mkSinkHook(
                    prefix + "/" + name,
                    entry.hash,
                    // N.B. this cast would not be acceptable in real
                    // code, because it would make an assert reachable,
                    // but it should harmless in this test.
                    static_cast<BlobMode>(entry.mode));
            },
            mockXpSettings);
    };

    mkSinkHook("", root.hash, BlobMode::Regular);

    ASSERT_EQ(files, files2);
}

TEST(GitLsRemote, parseSymrefLineWithReference) {
    auto line = "ref: refs/head/main	HEAD";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, "HEAD");
}

TEST(GitLsRemote, parseSymrefLineWithNoReference) {
    auto line = "ref: refs/head/main";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Symbolic);
    ASSERT_EQ(res->target, "refs/head/main");
    ASSERT_EQ(res->reference, std::nullopt);
}

TEST(GitLsRemote, parseObjectRefLine) {
    auto line = "abc123	refs/head/main";
    auto res = parseLsRemoteLine(line);
    ASSERT_TRUE(res.has_value());
    ASSERT_EQ(res->kind, LsRemoteRefLine::Kind::Object);
    ASSERT_EQ(res->target, "abc123");
    ASSERT_EQ(res->reference, "refs/head/main");
}

}
