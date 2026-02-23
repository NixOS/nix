#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "nix/util/file-descriptor.hh"
#include "nix/util/serialise.hh"
#include "nix/util/signals.hh"

#include <cstring>

#ifndef _WIN32
#  include <fcntl.h>
#  include <stdlib.h>
#endif

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

#ifndef _WIN32
TEST(ReadLine, TreatsEioAsEof)
{
    // Open a pty master. When the slave side is closed (or never opened),
    // reading from the master returns EIO, which readLine should treat as EOF.
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_NE(master, -1);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);

    // Open and immediately close the slave to trigger EIO on the master.
    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    ASSERT_NE(slave, -1);
    close(slave);

    // With eofOk=true, readLine should return empty string (treating EIO as EOF).
    EXPECT_EQ(readLine(master, /*eofOk=*/true), "");

    // With eofOk=false, readLine should throw EndOfFile.
    EXPECT_THROW(readLine(master), EndOfFile);

    close(master);
}

// macOS (BSD) discards buffered pty data on slave close and returns normal
// EOF (0) instead of EIO, so partial data never reaches the master.
#  ifdef __linux__
TEST(ReadLine, PartialLineBeforeEio)
{
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_NE(master, -1);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);

    int slave = open(ptsname(master), O_RDWR | O_NOCTTY);
    ASSERT_NE(slave, -1);

    // Write a partial line (no terminator) from the slave, then close it.
    ASSERT_EQ(::write(slave, "partial", 7), 7);
    close(slave);

    // readLine should return the partial data when eofOk=true.
    EXPECT_EQ(readLine(master, /*eofOk=*/true), "partial");

    close(master);
}
#  endif
#endif

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

TEST(WriteFull, RespectsAllowInterrupts)
{
    Pipe pipe;
    pipe.create();

    setInterrupted(true);

    // Must not throw Interrupted even though the interrupt flag is set.
    EXPECT_NO_THROW(writeFull(pipe.writeSide.get(), "hello", /*allowInterrupts=*/false));

    // Must throw Interrupted when allowInterrupts is true.
    EXPECT_THROW(writeFull(pipe.writeSide.get(), "hello", /*allowInterrupts=*/true), Interrupted);

    setInterrupted(false);
    pipe.writeSide.close();

    // Verify the data from the first write was actually written.
    FdSource source(pipe.readSide.get());
    EXPECT_EQ(source.readLine(/*eofOk=*/true), "hello");
}

} // namespace nix
