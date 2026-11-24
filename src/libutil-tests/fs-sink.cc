#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/util/archive.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/tests/gmock-matchers.hh"

namespace nix {

namespace {

/**
 * A small tree of every kind of file system object a sink can make.
 */
ref<MemorySourceAccessor> makeTree(bool withSymlink = true)
{
    auto accessor = make_ref<MemorySourceAccessor>();
    accessor->addDirectory(CanonPath::root);
    accessor->addFile("file"_filename, "contents");
    accessor->addFile("script"_filename, "#!/bin/sh\n", /*executable=*/true);
    accessor->addDirectory("dir"_filename);
    accessor->addFile("/dir/nested"_canonPath, "nested contents");
    accessor->addDirectory("/dir/empty"_canonPath);
    if (withSymlink)
        accessor->addSymlink("link"_filename, "dir/nested");
    return accessor;
}

/**
 * The tree at @p path in @p accessor as a NAR, so that trees can be
 * compared.
 */
std::string toNar(SourceAccessor & accessor, const CanonPath & path = CanonPath::root)
{
    StringSink sink;
    accessor.dumpPath(path, sink, defaultPathFilter, /*useCaseHack=*/false);
    return std::move(sink.s);
}

} // namespace

/* ----------------------------------------------------------------------------
 * copyRecursive
 * --------------------------------------------------------------------------*/

TEST(copyRecursive, copiesEverything)
{
    auto from = makeTree();
    auto to = make_ref<MemorySourceAccessor>();
    MemorySink sink{*to};
    copyRecursive(*from, CanonPath::root, sink);
    EXPECT_EQ(toNar(*to), toNar(*from));
}

TEST(copyRecursive, copiesSubtree)
{
    auto from = makeTree();
    auto to = make_ref<MemorySourceAccessor>();
    MemorySink sink{*to};
    copyRecursive(*from, "dir"_filename, sink);
    EXPECT_THAT(to, testing::HasDirectory(CanonPath::root, std::set<std::string>{"nested", "empty"}));
    EXPECT_EQ(toNar(*to), toNar(*from, "dir"_filename));
}

/* ----------------------------------------------------------------------------
 * RestoreSink
 * --------------------------------------------------------------------------*/

class RestoreSinkTest : public ::testing::Test
{
protected:
    AutoDelete tempDir{createTempDir()};

    void TearDown() override
    {
        /* Not just in the destructor, which would ignore any error. */
        tempDir.deletePath();
    }
};

TEST_F(RestoreSinkTest, restoresTree)
{
#ifdef _WIN32
    /* No symlinks or executable bits to restore. */
    auto from = makeTree(/*withSymlink=*/false);
#else
    auto from = makeTree();
#endif
    auto dst = tempDir.path() / "out";
    {
        auto sink = RestoreSink::forPath(dst, /*startFsync=*/false);
        copyRecursive(*from, CanonPath::root, sink);
    }
#ifndef _WIN32
    EXPECT_EQ(toNar(*makeFSSourceAccessor(dst)), toNar(*from));
#else
    EXPECT_THAT(makeFSSourceAccessor(dst), testing::HasContents("file"_filename, "contents"));
    EXPECT_THAT(makeFSSourceAccessor(dst), testing::HasContents("/dir/nested"_canonPath, "nested contents"));
#endif
}

TEST_F(RestoreSinkTest, restoresSingleFile)
{
    auto from = makeTree();
    auto dst = tempDir.path() / "out";
    {
        auto sink = RestoreSink::forPath(dst, /*startFsync=*/false);
        copyRecursive(*from, "file"_filename, sink);
    }
    EXPECT_EQ(readFile(dst), "contents");
}

TEST_F(RestoreSinkTest, refusesExisting)
{
    /* A sink only ever creates new entries. */
    auto dst = tempDir.path() / "out";
    writeFile(dst, "already here");
    EXPECT_THROW(
        {
            auto sink = RestoreSink::forPath(dst, /*startFsync=*/false);
            copyRecursive(*makeTree(), "file"_filename, sink);
        },
        SystemError);
    EXPECT_EQ(readFile(dst), "already here");
}

/* ----------------------------------------------------------------------------
 * RegularFileSink
 * --------------------------------------------------------------------------*/

TEST(RegularFileSink, passesOnContents)
{
    StringSink contents;
    RegularFileSink sink{contents};
    copyRecursive(*makeTree(), "file"_filename, sink);
    EXPECT_TRUE(sink.regular);
    EXPECT_EQ(contents.s, "contents");
}

TEST(RegularFileSink, notesOtherObjects)
{
    for (auto path : {"dir"_filename, "link"_filename}) {
        StringSink contents;
        RegularFileSink sink{contents};
        copyRecursive(*makeTree(), path, sink);
        EXPECT_FALSE(sink.regular) << path;
        EXPECT_EQ(contents.s, "") << path;
    }
}

/* ----------------------------------------------------------------------------
 * NullFileSystemObjectSink
 * --------------------------------------------------------------------------*/

TEST(NullFileSystemObjectSink, acceptsEverything)
{
    NullFileSystemObjectSink sink;
    EXPECT_NO_THROW(copyRecursive(*makeTree(), CanonPath::root, sink));
}

} // namespace nix
