#include "nix/util/memory-source-accessor.hh"
#include "nix/util/file-system-at.hh"
#include "nix/util/tests/json-characterization.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <functional>
#include <string_view>

namespace nix {

namespace memory_source_accessor {

using File = MemorySourceAccessor::File;

ref<MemorySourceAccessor> exampleSimple()
{
    auto sc = make_ref<MemorySourceAccessor>();
    sc->root = File{File::Regular{
        .executable = false,
        .contents = "asdf",
    }};
    return sc;
}

ref<MemorySourceAccessor> exampleComplex()
{
    using namespace std::literals;
    auto files = make_ref<MemorySourceAccessor>();
    files->root = File::Directory{
        .entries{
            {
                "foo",
                File::Regular{
                    .contents = "hello\n\0\n\tworld!"s,
                },
            },
            {
                "bar",
                File::Directory{
                    .entries =
                        {
                            {
                                "baz",
                                File::Regular{
                                    .executable = true,
                                    .contents = "good day,\n\0\n\tworld!"s,
                                },
                            },
                            {
                                "quux",
                                File::Symlink{
                                    .target = "/over/there",
                                },
                            },
                        },
                },
            },
        },
    };
    return files;
}

} // namespace memory_source_accessor

/* ----------------------------------------------------------------------------
 * MemorySourceAccessor
 * --------------------------------------------------------------------------*/

using ::nix::testing::HasSubstrIgnoreANSIMatcher;

class MemorySourceAccessorTestErrors : public ::testing::Test
{
protected:
    ref<MemorySourceAccessor> accessor = make_ref<MemorySourceAccessor>();
    MemorySink sink{*accessor};

    void SetUp() override
    {
        accessor->setPathDisplay("somepath");
    }
};

TEST_F(MemorySourceAccessorTestErrors, readFileNotFound)
{
    accessor->addDirectory(CanonPath::root);

    EXPECT_THAT(
        [&] { accessor->readFile(CanonPath("nonexistent")); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readFileNotARegularFile)
{
    accessor->addDirectory(CanonPath("subdir"));

    EXPECT_THAT(
        [&] { accessor->readFile(CanonPath("subdir")); },
        ThrowsMessage<NotARegularFile>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/subdir"), HasSubstrIgnoreANSIMatcher("is not a regular file"))));
}

TEST_F(MemorySourceAccessorTestErrors, readDirectoryNotFound)
{
    accessor->addDirectory(CanonPath::root);

    EXPECT_THAT(
        [&] { accessor->readDirectory(CanonPath("nonexistent")); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readDirectoryNotADirectory)
{
    accessor->addFile(CanonPath("file"), "");

    EXPECT_THAT(
        [&] { accessor->readDirectory(CanonPath("file")); },
        ThrowsMessage<NotADirectory>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, readLinkNotFound)
{
    accessor->addDirectory(CanonPath::root);

    EXPECT_THAT(
        [&] { accessor->readLink(CanonPath("nonexistent")); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readLinkNotASymlink)
{
    accessor->addFile(CanonPath("file"), "");

    EXPECT_THAT(
        [&] { accessor->readLink(CanonPath("file")); },
        ThrowsMessage<NotASymlink>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a symbolic link"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileParentNotDirectory)
{
    accessor->addFile(CanonPath("file"), "");

    EXPECT_THAT(
        [&] { accessor->addFile(CanonPath("file/child"), "contents"); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("file 'somepath/file' is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileNotARegularFile)
{
    accessor->addDirectory(CanonPath("subdir"));

    EXPECT_THAT(
        [&] { accessor->addFile(CanonPath("subdir"), "contents"); },
        ThrowsMessage<NotARegularFile>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/subdir"), HasSubstrIgnoreANSIMatcher("is not a regular file"))));
}

TEST_F(MemorySourceAccessorTestErrors, addDirectoryParentNotDirectory)
{
    accessor->addFile(CanonPath("file"), "");

    EXPECT_THAT(
        [&] { accessor->addDirectory(CanonPath("file/child")); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("file 'somepath/file' is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, addSymlinkParentNotDirectory)
{
    accessor->addFile(CanonPath("file"), "");

    EXPECT_THAT(
        [&] { accessor->addSymlink(CanonPath("file/child"), "target"); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("file 'somepath/file' is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, addSymlinkNotASymlink)
{
    accessor->addFile(CanonPath("file"), "");

    EXPECT_THAT(
        [&] { accessor->addSymlink(CanonPath("file"), "target"); },
        ThrowsMessage<NotASymlink>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a symbolic link"))));
}

TEST_F(MemorySourceAccessorTestErrors, createChildInvalidName)
{
    sink.createDirectory([&](auto & dir) {
        for (std::string_view name : {"", ".", "..", "a/b"})
            EXPECT_THROW(dir.createChild(name, [](auto & child) { child.createSymlink("target"); }), BadCanonPath)
                << name;
    });

    EXPECT_TRUE(accessor->readDirectory(CanonPath::root).empty());
}

TEST_F(MemorySourceAccessorTestErrors, createRootAlreadyExists)
{
    sink.createRegularFile(false, [](auto & crf) { crf("first"); });
    EXPECT_THAT(
        [&] { sink.createSymlink("second"); }, ThrowsMessage<Error>(HasSubstrIgnoreANSIMatcher("already exists")));

    EXPECT_EQ(accessor->readFile(CanonPath::root), "first");
}

TEST_F(MemorySourceAccessorTestErrors, createChildAlreadyExists)
{
    sink.createDirectory([](auto & dir) {
        dir.createChild("subdir", [](auto & child) {
            child.createDirectory([](auto & subdir) {
                subdir.createChild(
                    "file", [](auto & child) { child.createRegularFile(false, [](auto & crf) { crf("first"); }); });
                EXPECT_THAT(
                    [&] { subdir.createChild("file", [](auto & child) { child.createSymlink("second"); }); },
                    ThrowsMessage<Error>(HasSubstrIgnoreANSIMatcher("file 'somepath/subdir/file' already exists")));
            });
        });
    });

    EXPECT_EQ(accessor->readFile(CanonPath("subdir/file")), "first");
}

TEST_F(MemorySourceAccessorTestErrors, pathExistsThroughRegularFile)
{
    accessor->addFile(CanonPath("file"), "");
    EXPECT_FALSE(accessor->pathExists(CanonPath("file/child")));
}

TEST_F(MemorySourceAccessorTestErrors, maybeLstatThroughRegularFile)
{
    accessor->addFile(CanonPath("file"), "");
    EXPECT_FALSE(accessor->maybeLstat(CanonPath("file/child")));
}

TEST_F(MemorySourceAccessorTestErrors, pathExistsThroughSymlink)
{
    accessor->addSymlink(CanonPath("link"), "target");
    EXPECT_THAT(
        [&] { accessor->pathExists(CanonPath("link/child")); },
        ThrowsMessage<SymlinkNotAllowed>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/link"), HasSubstrIgnoreANSIMatcher("is a symlink"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileThroughSymlink)
{
    accessor->addSymlink(CanonPath("link"), "target");
    EXPECT_THAT(
        [&] { accessor->addFile(CanonPath("link/child"), "contents"); },
        ThrowsMessage<SymlinkNotAllowed>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/link"), HasSubstrIgnoreANSIMatcher("is a symlink"))));
}

/* ----------------------------------------------------------------------------
 * JSON
 * --------------------------------------------------------------------------*/

class MemorySourceAccessorTest : public virtual CharacterizationTest
{
    std::filesystem::path unitTestData = getUnitTestData() / "memory-source-accessor";

public:

    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData / testStem;
    }
};

using nlohmann::json;

struct MemorySourceAccessorJsonTest : MemorySourceAccessorTest,
                                      JsonCharacterizationTest<MemorySourceAccessor>,
                                      ::testing::WithParamInterface<std::pair<std::string_view, MemorySourceAccessor>>
{};

TEST_P(MemorySourceAccessorJsonTest, from_json)
{
    auto & [name, expected] = GetParam();
    /* Cannot use `readJsonTest` because need to compare `root` field of
       the source accessors for equality. */
    readTest(std::string{name} + ".json", [&](const auto & encodedRaw) {
        auto encoded = json::parse(encodedRaw);
        auto decoded = static_cast<MemorySourceAccessor>(encoded);
        ASSERT_EQ(decoded.root, expected.root);
    });
}

TEST_P(MemorySourceAccessorJsonTest, to_json)
{
    auto & [name, value] = GetParam();
    writeJsonTest(name, value);
}

INSTANTIATE_TEST_SUITE_P(
    MemorySourceAccessorJSON,
    MemorySourceAccessorJsonTest,
    ::testing::Values(
        std::pair{
            "simple",
            *memory_source_accessor::exampleSimple(),
        },
        std::pair{
            "complex",
            *memory_source_accessor::exampleComplex(),
        }));

} // namespace nix
