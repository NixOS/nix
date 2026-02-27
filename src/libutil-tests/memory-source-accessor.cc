#include "nix/util/memory-source-accessor.hh"
#include "nix/util/tests/json-characterization.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <string_view>

namespace nix {

namespace memory_source_accessor {

using namespace std::literals;
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
        sink.createDirectory(CanonPath::root);
    }
};

TEST_F(MemorySourceAccessorTestErrors, readFileNotFound)
{
    EXPECT_THAT(
        [&] { accessor->readFile(CanonPath("nonexistent")); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readFileNotARegularFile)
{
    sink.createDirectory(CanonPath("subdir"));

    EXPECT_THAT(
        [&] { accessor->readFile(CanonPath("subdir")); },
        ThrowsMessage<NotARegularFile>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/subdir"), HasSubstrIgnoreANSIMatcher("is not a regular file"))));
}

TEST_F(MemorySourceAccessorTestErrors, readDirectoryNotFound)
{
    EXPECT_THAT(
        [&] { accessor->readDirectory(CanonPath("nonexistent")); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readDirectoryNotADirectory)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { accessor->readDirectory(CanonPath("file")); },
        ThrowsMessage<NotADirectory>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, readLinkNotFound)
{
    EXPECT_THAT(
        [&] { accessor->readLink(CanonPath("nonexistent")); },
        ThrowsMessage<FileNotFound>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/nonexistent"), HasSubstrIgnoreANSIMatcher("does not exist"))));
}

TEST_F(MemorySourceAccessorTestErrors, readLinkNotASymlink)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { accessor->readLink(CanonPath("file")); },
        ThrowsMessage<NotASymlink>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a symbolic link"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileParentNotDirectory)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { accessor->addFile(CanonPath("file/child"), "contents"); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("cannot be created because some parent file is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, addFileNotARegularFile)
{
    sink.createDirectory(CanonPath("subdir"));

    EXPECT_THAT(
        [&] { accessor->addFile(CanonPath("subdir"), "contents"); },
        ThrowsMessage<NotARegularFile>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/subdir"), HasSubstrIgnoreANSIMatcher("is not a regular file"))));
}

TEST_F(MemorySourceAccessorTestErrors, createDirectoryParentNotDirectory)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { sink.createDirectory(CanonPath("file/child")); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("cannot be created because some parent file is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, createDirectoryNotADirectory)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { sink.createDirectory(CanonPath("file")); },
        ThrowsMessage<NotADirectory>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, createRegularFileParentNotDirectory)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { sink.createRegularFile(CanonPath("file/child"), [](CreateRegularFileSink &) {}); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("file/child"),
            HasSubstrIgnoreANSIMatcher("cannot be created because some parent file is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, createRegularFileNotARegularFile)
{
    sink.createDirectory(CanonPath("subdir"));

    EXPECT_THAT(
        [&] { sink.createRegularFile(CanonPath("subdir"), [](CreateRegularFileSink &) {}); },
        ThrowsMessage<NotARegularFile>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/subdir"), HasSubstrIgnoreANSIMatcher("is not a regular file"))));
}

TEST_F(MemorySourceAccessorTestErrors, createSymlinkParentNotDirectory)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { sink.createSymlink(CanonPath("file/child"), "target"); },
        ThrowsMessage<Error>(AllOf(
            HasSubstrIgnoreANSIMatcher("somepath/file/child"),
            HasSubstrIgnoreANSIMatcher("cannot be created because some parent file is not a directory"))));
}

TEST_F(MemorySourceAccessorTestErrors, createSymlinkNotASymlink)
{
    sink.createRegularFile(CanonPath("file"), [](CreateRegularFileSink &) {});

    EXPECT_THAT(
        [&] { sink.createSymlink(CanonPath("file"), "target"); },
        ThrowsMessage<NotASymlink>(
            AllOf(HasSubstrIgnoreANSIMatcher("somepath/file"), HasSubstrIgnoreANSIMatcher("is not a symbolic link"))));
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
