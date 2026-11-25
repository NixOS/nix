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
                "foo"_filename,
                File::Regular{
                    .contents = "hello\n\0\n\tworld!"s,
                },
            },
            {
                "bar"_filename,
                File::Directory{
                    .entries =
                        {
                            {
                                "baz"_filename,
                                File::Regular{
                                    .executable = true,
                                    .contents = "good day,\n\0\n\tworld!"s,
                                },
                            },
                            {
                                "quux"_filename,
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
        [&] { accessor->readFile("nonexistent"_filename); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readFileNotARegularFile)
{
    accessor->addDirectory("subdir"_filename);

    EXPECT_THAT(
        [&] { accessor->readFile("subdir"_filename); },
        ThrowsMessage<NotARegularFile>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/subdir"), HasSubstrIgnoreANSIMatcher("is not a regular file"))));
}

TEST_F(MemorySourceAccessorTestErrors, readDirectoryNotFound)
{
    accessor->addDirectory(CanonPath::root);

    EXPECT_THAT(
        [&] { accessor->readDirectory("nonexistent"_filename); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readDirectoryNotADirectory)
{
    accessor->addFile("file"_filename, "");

    EXPECT_THAT(
        [&] { accessor->readDirectory("file"_filename); },
        ThrowsMessage<NotADirectory>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, readLinkNotFound)
{
    accessor->addDirectory(CanonPath::root);

    EXPECT_THAT(
        [&] { accessor->readLink("nonexistent"_filename); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readLinkNotASymlink)
{
    accessor->addFile("file"_filename, "");

    EXPECT_THAT(
        [&] { accessor->readLink("file"_filename); },
        ThrowsMessage<NotASymlink>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a symbolic link"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileParentNotDirectory)
{
    accessor->addFile("file"_filename, "");

    EXPECT_THAT(
        [&] { accessor->addFile("/file/child"_canonPath, "contents"); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("file 'somepath/file' is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileNotARegularFile)
{
    accessor->addDirectory("subdir"_filename);

    EXPECT_THAT(
        [&] { accessor->addFile("subdir"_filename, "contents"); },
        ThrowsMessage<NotARegularFile>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/subdir"), HasSubstrIgnoreANSIMatcher("is not a regular file"))));
}

TEST_F(MemorySourceAccessorTestErrors, addDirectoryParentNotDirectory)
{
    accessor->addFile("file"_filename, "");

    EXPECT_THAT(
        [&] { accessor->addDirectory("/file/child"_canonPath); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("file 'somepath/file' is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, addSymlinkParentNotDirectory)
{
    accessor->addFile("file"_filename, "");

    EXPECT_THAT(
        [&] { accessor->addSymlink("/file/child"_canonPath, "target"); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("file 'somepath/file' is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, addSymlinkNotASymlink)
{
    accessor->addFile("file"_filename, "");

    EXPECT_THAT(
        [&] { accessor->addSymlink("file"_filename, "target"); },
        ThrowsMessage<NotASymlink>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a symbolic link"))));
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
        dir.createChild("subdir"_filename, [](auto & child) {
            child.createDirectory([](auto & subdir) {
                subdir.createChild("file"_filename, [](auto & child) {
                    child.createRegularFile(false, [](auto & crf) { crf("first"); });
                });
                EXPECT_THAT(
                    [&] { subdir.createChild("file"_filename, [](auto & child) { child.createSymlink("second"); }); },
                    ThrowsMessage<Error>(HasSubstrIgnoreANSIMatcher("file 'somepath/subdir/file' already exists")));
            });
        });
    });

    EXPECT_EQ(accessor->readFile("/subdir/file"_canonPath), "first");
}

TEST_F(MemorySourceAccessorTestErrors, pathExistsThroughRegularFile)
{
    accessor->addFile("file"_filename, "");
    EXPECT_FALSE(accessor->pathExists("/file/child"_canonPath));
}

TEST_F(MemorySourceAccessorTestErrors, maybeLstatThroughRegularFile)
{
    accessor->addFile("file"_filename, "");
    EXPECT_FALSE(accessor->maybeLstat("/file/child"_canonPath));
}

TEST_F(MemorySourceAccessorTestErrors, pathExistsThroughSymlink)
{
    accessor->addSymlink("link"_filename, "target");
    EXPECT_THAT(
        [&] { accessor->pathExists("/link/child"_canonPath); },
        ThrowsMessage<SymlinkNotAllowed>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/link"), HasSubstrIgnoreANSIMatcher("is a symlink"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileThroughSymlink)
{
    accessor->addSymlink("link"_filename, "target");
    EXPECT_THAT(
        [&] { accessor->addFile("/link/child"_canonPath, "contents"); },
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

/* ----------------------------------------------------------------------------
 * resolveSymlinks
 * --------------------------------------------------------------------------*/

TEST(MemorySourceAccessorResolveSymlinks, dotDotAfterSymlink)
{
    auto accessor = make_ref<MemorySourceAccessor>();
    accessor->addDirectory("a"_filename);
    accessor->addDirectory("/a/b"_canonPath);
    accessor->addFile("/a/b/file"_canonPath, "");
    accessor->addSymlink("link"_filename, "a/b");

    /* The parent of the link's target, not the directory containing
       the link, as removing `..` lexically would have it. */
    EXPECT_EQ(accessor->resolveSymlinks(accessor->resolvePath(UnresolvedPath("/link/.."))), "/a"_canonPath);
    EXPECT_EQ(
        accessor->resolveSymlinks(accessor->resolvePath(UnresolvedPath("/link/../b/file"))), "/a/b/file"_canonPath);
}

TEST(MemorySourceAccessorResolveSymlinks, infiniteRecursionSaysWhere)
{
    auto accessor = make_ref<MemorySourceAccessor>();
    accessor->addSymlink("loop"_filename, "loop");

    /* The error is about where it went wrong, the trace about the
       whole path. */
    EXPECT_THAT(
        [&] { accessor->resolveSymlinks("/loop/x"_canonPath); },
        ::testing::ThrowsMessage<Error>(::testing::AllOf(
            testing::HasSubstrIgnoreANSIMatcher("infinite symlink recursion at"),
            testing::HasSubstrIgnoreANSIMatcher("while resolving the symlinks in"))));
    EXPECT_THAT(
        [&] { accessor->resolvePath(UnresolvedPath("/loop/..")); },
        ::testing::ThrowsMessage<Error>(::testing::AllOf(
            testing::HasSubstrIgnoreANSIMatcher("infinite symlink recursion at"),
            testing::HasSubstrIgnoreANSIMatcher("while resolving '/loop/..'"))));
}

TEST(MemorySourceAccessorResolveSymlinks, absoluteTarget)
{
    auto accessor = make_ref<MemorySourceAccessor>();
    accessor->addDirectory("a"_filename);
    accessor->addDirectory("/a/b"_canonPath);
    accessor->addFile("/a/b/file"_canonPath, "");
    accessor->addDirectory("x"_filename);
    accessor->addSymlink("/x/link"_canonPath, "/a/b");

    EXPECT_EQ(accessor->resolveSymlinks(accessor->resolvePath(UnresolvedPath("/x/link/file"))), "/a/b/file"_canonPath);
    EXPECT_EQ(accessor->resolveSymlinks(accessor->resolvePath(UnresolvedPath("/x/link/.."))), "/a"_canonPath);
}

TEST(MemorySourceAccessorResolvePath, onlyAsNeeded)
{
    auto accessor = make_ref<MemorySourceAccessor>();
    accessor->addDirectory("a"_filename);
    accessor->addDirectory("/a/b"_canonPath);
    accessor->addFile("/a/file"_canonPath, "");
    accessor->addSymlink("link"_filename, "a/b");
    accessor->addSymlink("/a/filelink"_canonPath, "file");

    /* The symlink before the `..` is resolved, so it is the parent of
       its target... */
    EXPECT_EQ(accessor->resolvePath(UnresolvedPath("/link/../file")), "/a/file"_canonPath);
    /* ...but those after it are kept. */
    EXPECT_EQ(accessor->resolvePath(UnresolvedPath("/link/../filelink")), "/a/filelink"_canonPath);
    /* So, without `..`, nothing is resolved. */
    EXPECT_EQ(accessor->resolvePath(UnresolvedPath("/link")), "/link"_canonPath);
    EXPECT_EQ(accessor->resolvePath(UnresolvedPath("/a/filelink")), "/a/filelink"_canonPath);
    /* Nor does it need anything to exist then. */
    EXPECT_EQ(accessor->resolvePath(UnresolvedPath("/nonexistent/x")), "/nonexistent/x"_canonPath);
}

} // namespace nix
