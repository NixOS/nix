#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-system.hh"
#include "nix/util/fs-sink.hh"

#include <cstring>

namespace nix {

// BufferedSource with configurable small buffer for precise boundary testing.
struct TestBufferedStringSource : BufferedSource
{
    std::string_view data;
    size_t pos = 0;

    TestBufferedStringSource(std::string_view d, size_t bufSize)
        : BufferedSource(bufSize)
        , data(d)
    {
    }

protected:
    size_t readUnbuffered(char * buf, size_t len) override
    {
        if (pos >= data.size())
            throw EndOfFile("end of test data");
        size_t n = std::min(len, data.size() - pos);
        std::memcpy(buf, data.data() + pos, n);
        pos += n;
        return n;
    }
};

TEST(ReadLine, ReadsLinesFromPipe)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "hello\nworld\n", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    EXPECT_EQ(readLine(pipe.readSide.get()), "hello");
    EXPECT_EQ(readLine(pipe.readSide.get()), "world");
    EXPECT_EQ(readLine(pipe.readSide.get(), /*eofOk=*/true), "");
}

TEST(ReadLine, ReturnsPartialLineOnEofWhenAllowed)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "partial", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    EXPECT_EQ(readLine(pipe.readSide.get(), /*eofOk=*/true), "partial");
    EXPECT_EQ(readLine(pipe.readSide.get(), /*eofOk=*/true), "");
}

TEST(ReadLine, ThrowsOnEofWhenNotAllowed)
{
    Pipe pipe;
    pipe.create();
    pipe.writeSide.close();

    EXPECT_THROW(readLine(pipe.readSide.get()), EndOfFile);
}

TEST(ReadLine, EmptyLine)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "\n", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    EXPECT_EQ(readLine(pipe.readSide.get()), "");
}

TEST(ReadLine, ConsecutiveEmptyLines)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "\n\n\n", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    EXPECT_EQ(readLine(pipe.readSide.get()), "");
    EXPECT_EQ(readLine(pipe.readSide.get()), "");
    EXPECT_EQ(readLine(pipe.readSide.get()), "");
    EXPECT_EQ(readLine(pipe.readSide.get(), /*eofOk=*/true), "");
}

TEST(ReadLine, LineWithNullBytes)
{
    Pipe pipe;
    pipe.create();

    std::string data(
        "a\x00"
        "b\n",
        4);
    writeFull(pipe.writeSide.get(), data, /*allowInterrupts=*/false);
    pipe.writeSide.close();

    auto line = readLine(pipe.readSide.get());
    EXPECT_EQ(line.size(), 3);
    EXPECT_EQ(
        line,
        std::string(
            "a\x00"
            "b",
            3));
}

TEST(BufferedSourceReadLine, ReadsLinesFromPipe)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "hello\nworld\n", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    FdSource source(pipe.readSide.get());

    EXPECT_EQ(source.readLine(), "hello");
    EXPECT_EQ(source.readLine(), "world");
    EXPECT_EQ(source.readLine(/*eofOk=*/true), "");
}

TEST(BufferedSourceReadLine, ReturnsPartialLineOnEofWhenAllowed)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "partial", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    FdSource source(pipe.readSide.get());

    EXPECT_EQ(source.readLine(/*eofOk=*/true), "partial");
    EXPECT_EQ(source.readLine(/*eofOk=*/true), "");
}

TEST(BufferedSourceReadLine, ThrowsOnEofWhenNotAllowed)
{
    Pipe pipe;
    pipe.create();
    pipe.writeSide.close();

    FdSource source(pipe.readSide.get());

    EXPECT_THROW(source.readLine(), EndOfFile);
}

TEST(BufferedSourceReadLine, EmptyLine)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "\n", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    FdSource source(pipe.readSide.get());

    EXPECT_EQ(source.readLine(), "");
}

TEST(BufferedSourceReadLine, ConsecutiveEmptyLines)
{
    Pipe pipe;
    pipe.create();

    writeFull(pipe.writeSide.get(), "\n\n\n", /*allowInterrupts=*/false);
    pipe.writeSide.close();

    FdSource source(pipe.readSide.get());

    EXPECT_EQ(source.readLine(), "");
    EXPECT_EQ(source.readLine(), "");
    EXPECT_EQ(source.readLine(), "");
    EXPECT_EQ(source.readLine(/*eofOk=*/true), "");
}

TEST(BufferedSourceReadLine, LineWithNullBytes)
{
    Pipe pipe;
    pipe.create();

    std::string data(
        "a\x00"
        "b\n",
        4);
    writeFull(pipe.writeSide.get(), data, /*allowInterrupts=*/false);
    pipe.writeSide.close();

    FdSource source(pipe.readSide.get());

    auto line = source.readLine();
    EXPECT_EQ(line.size(), 3);
    EXPECT_EQ(
        line,
        std::string(
            "a\x00"
            "b",
            3));
}

TEST(BufferedSourceReadLine, NewlineAtBufferBoundary)
{
    // "abc\n" with buf=4: newline is last byte, triggers buffer reset.
    TestBufferedStringSource source("abc\nxyz\n", 4);

    EXPECT_EQ(source.readLine(), "abc");
    EXPECT_FALSE(source.hasData());
    EXPECT_EQ(source.readLine(), "xyz");
}

TEST(BufferedSourceReadLine, DataRemainsAfterReadLine)
{
    // "ab\ncd\n" with buf=8: all fits in buffer, data remains after first read.
    TestBufferedStringSource source("ab\ncd\n", 8);

    EXPECT_EQ(source.readLine(), "ab");
    EXPECT_TRUE(source.hasData());
    EXPECT_EQ(source.readLine(), "cd");
}

TEST(BufferedSourceReadLine, LineSpansMultipleRefills)
{
    // 10-char line with buf=4 requires 3 buffer refills.
    TestBufferedStringSource source("0123456789\n", 4);

    EXPECT_EQ(source.readLine(), "0123456789");
}

TEST(BufferedSourceReadLine, BufferExhaustedThenEof)
{
    // 8 chars with buf=4: two refills, then EOF with partial line.
    TestBufferedStringSource source("abcdefgh", 4);

    EXPECT_EQ(source.readLine(/*eofOk=*/true), "abcdefgh");
    EXPECT_EQ(source.readLine(/*eofOk=*/true), "");
}

/* ----------------------------------------------------------------------------
 * readLinkAt
 * --------------------------------------------------------------------------*/

TEST(readLinkAt, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    constexpr size_t maxPathLength =
#ifdef _WIN32
        260
#else
        PATH_MAX
#endif
        ;
    std::string mediumTarget(maxPathLength / 2, 'x');
    std::string longTarget(maxPathLength - 1, 'y');

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir);
        sink.createSymlink(CanonPath("link"), "target");
        sink.createSymlink(CanonPath("relative"), "../relative/path");
        sink.createSymlink(CanonPath("absolute"), "/absolute/path");
        sink.createSymlink(CanonPath("medium"), mediumTarget);
        sink.createSymlink(CanonPath("long"), longTarget);
        sink.createDirectory(CanonPath("a"));
        sink.createDirectory(CanonPath("a/b"));
        sink.createSymlink(CanonPath("a/b/link"), "nested_target");
        sink.createRegularFile(CanonPath("regular"), [](CreateRegularFileSink &) {});
        sink.createDirectory(CanonPath("dir"));
    }

    AutoCloseFD dirFd = openDirectory(tmpDir);

    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("link")), OS_STR("target"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("relative")), OS_STR("../relative/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("absolute")), OS_STR("/absolute/path"));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("medium")), string_to_os_string(mediumTarget));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("long")), string_to_os_string(longTarget));
    EXPECT_EQ(readLinkAt(dirFd.get(), CanonPath("a/b/link")), OS_STR("nested_target"));

    AutoCloseFD subDirFd = openDirectory(tmpDir / "a");
    EXPECT_EQ(readLinkAt(subDirFd.get(), CanonPath("b/link")), OS_STR("nested_target"));

    // Test error cases - expect SystemError on both platforms
    EXPECT_THROW(readLinkAt(dirFd.get(), CanonPath("regular")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), CanonPath("dir")), SystemError);
    EXPECT_THROW(readLinkAt(dirFd.get(), CanonPath("nonexistent")), SystemError);
}

/* ----------------------------------------------------------------------------
 * openFileEnsureBeneathNoSymlinks
 * --------------------------------------------------------------------------*/

TEST(openFileEnsureBeneathNoSymlinks, works)
{
    std::filesystem::path tmpDir = nix::createTempDir();
    nix::AutoDelete delTmpDir(tmpDir, /*recursive=*/true);

    {
        RestoreSink sink(/*startFsync=*/false);
        sink.dstPath = tmpDir;
        sink.dirFd = openDirectory(tmpDir);
        sink.createDirectory(CanonPath("a"));
        sink.createDirectory(CanonPath("c"));
        sink.createDirectory(CanonPath("c/d"));
        sink.createRegularFile(CanonPath("c/d/regular"), [](CreateRegularFileSink & crf) { crf("some contents"); });
        sink.createSymlink(CanonPath("a/absolute_symlink"), tmpDir.string());
        sink.createSymlink(CanonPath("a/relative_symlink"), "../.");
        sink.createSymlink(CanonPath("a/broken_symlink"), "./nonexistent");
        sink.createDirectory(CanonPath("a/b"), [](FileSystemObjectSink & dirSink, const CanonPath & relPath) {
            dirSink.createDirectory(CanonPath("d"));
            dirSink.createSymlink(CanonPath("c"), "./d");
        });
        // FIXME: This still follows symlinks on Unix (incorrectly succeeds)
        sink.createDirectory(CanonPath("a/b/c/e"));
        // Test that symlinks in intermediate path are detected during nested operations
        ASSERT_THROW(
            sink.createDirectory(
                CanonPath("a/b/c/f"), [](FileSystemObjectSink & dirSink, const CanonPath & relPath) {}),
            SymlinkNotAllowed);
        ASSERT_THROW(
            sink.createRegularFile(
                CanonPath("a/b/c/regular"), [](CreateRegularFileSink & crf) { crf("some contents"); }),
            SymlinkNotAllowed);
    }

    AutoCloseFD dirFd = openDirectory(tmpDir);

    // Helper to open files with platform-specific arguments
    auto openRead = [&](std::string_view path) -> Descriptor {
        return openFileEnsureBeneathNoSymlinks(
            dirFd.get(),
            CanonPath(path),
#ifdef _WIN32
            FILE_READ_DATA | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            0
#else
            O_RDONLY,
            0
#endif
        );
    };

    auto openReadDir = [&](std::string_view path) -> Descriptor {
        return openFileEnsureBeneathNoSymlinks(
            dirFd.get(),
            CanonPath(path),
#ifdef _WIN32
            FILE_READ_ATTRIBUTES | SYNCHRONIZE,
            FILE_DIRECTORY_FILE
#else
            O_RDONLY | O_DIRECTORY,
            0
#endif
        );
    };

    auto openCreateExclusive = [&](std::string_view path) -> Descriptor {
        return openFileEnsureBeneathNoSymlinks(
            dirFd.get(),
            CanonPath(path),
#ifdef _WIN32
            FILE_WRITE_DATA | SYNCHRONIZE,
            0,
            FILE_CREATE // Create new file, fail if exists (equivalent to O_CREAT | O_EXCL)
#else
            O_CREAT | O_WRONLY | O_EXCL,
            0666
#endif
        );
    };

    // Test that symlinks are detected and rejected
    EXPECT_THROW(openRead("a/absolute_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/relative_symlink"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/absolute_symlink/a"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/absolute_symlink/c/d"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/relative_symlink/c"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/b/c/d"), SymlinkNotAllowed);
    EXPECT_THROW(openRead("a/broken_symlink"), SymlinkNotAllowed);

#if !defined(_WIN32) && !defined(__CYGWIN__)
    // This returns ELOOP on cygwin when O_NOFOLLOW is used
    EXPECT_EQ(openCreateExclusive("a/broken_symlink"), INVALID_DESCRIPTOR);
    /* Sanity check, no symlink shenanigans and behaves the same as regular openat with O_EXCL | O_CREAT. */
    EXPECT_EQ(errno, EEXIST);
#endif
    EXPECT_THROW(openCreateExclusive("a/absolute_symlink/broken_symlink"), SymlinkNotAllowed);

    // Test invalid paths
    EXPECT_EQ(openRead("c/d/regular/a"), INVALID_DESCRIPTOR);
    EXPECT_EQ(openReadDir("c/d/regular"), INVALID_DESCRIPTOR);

    // Test valid paths work
    EXPECT_TRUE(AutoCloseFD{openRead("c/d/regular")});
    EXPECT_TRUE(AutoCloseFD{openCreateExclusive("a/regular")});
}

} // namespace nix
