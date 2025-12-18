#include <gtest/gtest.h>

#include "nix/util/file-descriptor.hh"

#ifndef _WIN32

#  include <unistd.h>

namespace nix {

TEST(ReadLine, ReadsLinesFromPipe)
{
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    AutoCloseFD readSide{fds[0]};
    AutoCloseFD writeSide{fds[1]};

    writeFull(writeSide.get(), "hello\nworld\n", /*allowInterrupts=*/false);
    writeSide.close();

    EXPECT_EQ(readLine(readSide.get()), "hello");
    EXPECT_EQ(readLine(readSide.get()), "world");
    EXPECT_EQ(readLine(readSide.get(), /*eofOk=*/true), "");
    EXPECT_EQ(readLine(readSide.get(), /*eofOk=*/true), "");
}

TEST(ReadLine, ReturnsPartialLineOnEofWhenAllowed)
{
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    AutoCloseFD readSide{fds[0]};
    AutoCloseFD writeSide{fds[1]};

    writeFull(writeSide.get(), "partial", /*allowInterrupts=*/false);
    writeSide.close();

    EXPECT_EQ(readLine(readSide.get(), /*eofOk=*/true), "partial");
    EXPECT_EQ(readLine(readSide.get(), /*eofOk=*/true), "");
}

TEST(ReadLine, ThrowsOnEofWhenNotAllowed)
{
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);

    AutoCloseFD readSide{fds[0]};
    AutoCloseFD writeSide{fds[1]};

    writeSide.close();

    EXPECT_THROW(readLine(readSide.get()), EndOfFile);
}

} // namespace nix

#endif
