#include "nix/util/serialise-async.hh"
#include "nix/util/finally.hh"
#include "nix/util/archive.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <csignal>
#include <boost/asio/bind_cancellation_slot.hpp>
#include <boost/asio/cancellation_signal.hpp>
#include <boost/asio/error.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/system/system_error.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdint>
#include <chrono>
#include <future>

#include <fcntl.h>
#include <unistd.h>

namespace nix {

class AsyncFdTest : public ::testing::Test
{
protected:
    asio::io_context ctx;

    void runBounded()
    {
        ctx.run_for(std::chrono::seconds(5));
    }

    template<typename T>
    static bool ready(const std::future<T> & f)
    {
        return f.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
    }
};

/* Note that in practice we'd want to run with the protected_fixedsize_stack allocator. */

TEST_F(AsyncFdTest, dumpStringThroughPipe)
{
    Pipe pipe;
    pipe.create();

    std::string contents(1000000, 'x');

    auto write = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSink sink(ctx, yield, std::move(pipe.writeSide));
            dumpString(contents, sink);
            sink.flush();
        },
        asio::use_future);

    auto read = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSource source(ctx, yield, std::move(pipe.readSide));
            std::unique_ptr<MemorySourceAccessor> accessor = std::make_unique<MemorySourceAccessor>();
            MemorySink fsSink(*accessor);
            parseDump(fsSink, source);
            return accessor;
        },
        asio::use_future);

    runBounded();
    ASSERT_TRUE(ready(read));
    ASSERT_TRUE(ready(write));

    std::unique_ptr<MemorySourceAccessor> accessor;

    ASSERT_NO_THROW(accessor = read.get());
    ASSERT_NO_THROW(write.get());
    EXPECT_THAT(accessor, ::nix::testing::HasContents(CanonPath::root, contents));
}

/* Every 8 bytes carry their own index, so a part resent from the wrong offset shows up. */
TEST_F(AsyncFdTest, partialWritesContinueWhereTheyStopped)
{
    Pipe pipe;
    pipe.create();
#ifdef F_SETPIPE_SZ
    (void) ::fcntl(pipe.writeSide.get(), F_SETPIPE_SZ, 4096);
#endif

    std::string contents;
    for (uint64_t i = 0; contents.size() < (4u << 20); ++i)
        contents.append(reinterpret_cast<const char *>(&i), sizeof i);

    auto write = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSink sink(ctx, yield, std::move(pipe.writeSide));
            sink(contents);
            sink.flush();
        },
        asio::use_future);

    auto read = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSource source(ctx, yield, std::move(pipe.readSide));
            std::string got(contents.size(), '\0');
            source(got.data(), got.size());
            char c;
            EXPECT_THROW(source(&c, 1), EndOfFile);
            return got;
        },
        asio::use_future);

    runBounded();
    ASSERT_TRUE(ready(write));
    ASSERT_TRUE(ready(read)) << "the reader did not get every byte";
    ASSERT_NO_THROW(write.get());
    std::string got;
    ASSERT_NO_THROW(got = read.get());
    auto [mismatch, _] = std::mismatch(contents.begin(), contents.end(), got.begin());
    EXPECT_TRUE(mismatch == contents.end()) << "first wrong byte at offset " << (mismatch - contents.begin());
}

/* A starts B, which asio runs at once up to its first suspension. */
TEST_F(AsyncFdTest, suspendedCoroutinesKeepTheirOwnActivity)
{
    Pipe pipeA, pipeB;
    pipeA.create();
    pipeB.create();

    const ActivityId actA = 1001, actB = 2002;
    std::future<ActivityId> seenByB;
    ActivityId seenByAAfterSpawn = 0;

    auto seenByA = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            ActivityId seen;
            {
                PushActivity push(actA);
                {
                    PushActivity keep(getCurActivity());
                    seenByB = asio::spawn(
                        ctx,
                        [&](asio::yield_context yield) {
                            PushActivity push(actB);
                            AsyncFdSource source(ctx, yield, std::move(pipeB.readSide));
                            char c;
                            source(&c, 1);
                            return getCurActivity();
                        },
                        asio::use_future);
                }
                seenByAAfterSpawn = getCurActivity();
                AsyncFdSource source(ctx, yield, std::move(pipeA.readSide));
                char c;
                source(&c, 1);
                seen = getCurActivity();
            }
            /* A's activity is popped; only now may B finish. */
            writeFull(pipeB.writeSide.get(), "b");
            return seen;
        },
        asio::use_future);

    asio::post(ctx, [&] { writeFull(pipeA.writeSide.get(), "a"); });

    {
        PushActivity keep(getCurActivity());
        runBounded();
    }
    ASSERT_TRUE(ready(seenByA));
    ASSERT_TRUE(ready(seenByB));
    EXPECT_EQ(seenByAAfterSpawn, actA);
    EXPECT_EQ(seenByA.get(), actA);
    EXPECT_EQ(seenByB.get(), actB);
    EXPECT_EQ(getCurActivity(), 0u);
}

TEST_F(AsyncFdTest, writeToClosedPipe)
{
    Pipe pipe;
    pipe.create();
    /* Nix programs ignore SIGPIPE, so the write fails with EPIPE instead. */
    auto oldHandler = ::signal(SIGPIPE, SIG_IGN);
    Finally restoreHandler([&] { ::signal(SIGPIPE, oldHandler); });
    pipe.readSide.close();

    auto write = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSink sink(ctx, yield, std::move(pipe.writeSide));
            sink << "hello";
            sink.flush();
        },
        asio::use_future);

    runBounded();
    ASSERT_TRUE(ready(write));
    EXPECT_THROW(write.get(), SystemError);
}

TEST_F(AsyncFdTest, eofThrowsEndOfFile)
{
    Pipe pipe;
    pipe.create();
    pipe.writeSide.close();

    auto read = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSource source(ctx, yield, std::move(pipe.readSide));
            source.endOfFileError = "the other side went away";
            char c;
            source(&c, 1);
        },
        asio::use_future);

    runBounded();
    ASSERT_TRUE(ready(read));
    try {
        read.get();
        FAIL() << "no EndOfFile was thrown";
    } catch (EndOfFile & e) {
        EXPECT_THAT(e.what(), ::testing::HasSubstr("the other side went away"));
    }
}

TEST_F(AsyncFdTest, hasDataProbesTheDescriptorThroughTheBase)
{
    Pipe pipe;
    pipe.create();

    auto probe = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSource source(ctx, yield, std::move(pipe.readSide));
            BufferedSource & base = source;
            bool before = base.hasData();
            writeFull(pipe.writeSide.get(), "x");
            bool after = base.hasData();
            return std::pair{before, after};
        },
        asio::use_future);

    runBounded();
    ASSERT_TRUE(ready(probe));
    auto [before, after] = probe.get();
    EXPECT_FALSE(before);
    EXPECT_TRUE(after);
}

TEST_F(AsyncFdTest, terminalCancellationAbortsASuspendedRead)
{
    Pipe pipe;
    pipe.create();

    asio::cancellation_signal cancel;
    std::exception_ptr error;
    bool completed = false;

    asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            AsyncFdSource source(ctx, yield, std::move(pipe.readSide));
            char c;
            source(&c, 1);
        },
        asio::bind_cancellation_slot(cancel.slot(), [&](std::exception_ptr e) {
            completed = true;
            error = e;
        }));

    ctx.poll();
    ASSERT_FALSE(completed);

    asio::post(ctx, [&]() { cancel.emit(asio::cancellation_type::terminal); });
    runBounded();

    ASSERT_TRUE(completed);
    ASSERT_TRUE(error);
    try {
        std::rethrow_exception(error);
    } catch (SystemError & e) {
        EXPECT_EQ(e.ec(), std::error_code(asio::error::make_error_code(asio::error::operation_aborted)));
    } catch (...) {
        FAIL() << "expected a SystemError carrying operation_aborted";
    }
}

/* A cancellation while no operation is pending only marks the coroutine;
   `throw_if_cancelled(true)` then fails the next one, a write that would hang. */
TEST_F(AsyncFdTest, terminalCancellationBetweenOperationsFailsTheNextOne)
{
    Pipe pipe;
    pipe.create();

    {
        int fd = pipe.writeSide.get();
        int flags = ::fcntl(fd, F_GETFL);
        ASSERT_NE(flags, -1);
        ASSERT_NE(::fcntl(fd, F_SETFL, flags | O_NONBLOCK), -1);
        std::array<char, 4096> chunk{};
        for (size_t size : {chunk.size(), size_t(1)}) {
            while (true) {
                auto n = ::write(fd, chunk.data(), size);
                if (n > 0)
                    continue;
                ASSERT_EQ(n, -1);
                if (errno == EINTR)
                    continue;
                ASSERT_TRUE(errno == EAGAIN || errno == EWOULDBLOCK) << "errno " << errno;
                break;
            }
        }
    }

    asio::cancellation_signal cancel;
    std::exception_ptr error;
    bool completed = false;
    bool suspended = false;
    bool cancelledWhileSuspended = false;
    bool resumed = false;

    asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            yield.throw_if_cancelled(true);
            Finally noLongerThrowIfCancelled([&] { yield.throw_if_cancelled(false); });

            AsyncFdSink sink(ctx, yield, std::move(pipe.writeSide));

            /* Queued ahead of our own resumption below. */
            asio::post(ctx, [&]() {
                cancelledWhileSuspended = suspended;
                cancel.emit(asio::cancellation_type::terminal);
            });
            suspended = true;
            asio::post(ctx, yield);
            suspended = false;
            resumed = true;

            sink << "x";
            sink.flush();
        },
        asio::bind_cancellation_slot(cancel.slot(), [&](std::exception_ptr e) {
            completed = true;
            error = e;
        }));

    runBounded();

    ASSERT_TRUE(resumed);
    ASSERT_TRUE(cancelledWhileSuspended);
    ASSERT_TRUE(completed) << "the write after the cancellation is still waiting";
    ASSERT_TRUE(error);
    const auto aborted = asio::error::make_error_code(asio::error::operation_aborted);
    try {
        std::rethrow_exception(error);
    } catch (boost::system::system_error & e) {
        EXPECT_EQ(e.code(), aborted);
    } catch (SystemError & e) {
        EXPECT_EQ(e.ec(), std::error_code(aborted));
    } catch (...) {
        FAIL() << "expected the write to fail with operation_aborted";
    }
}

} // namespace nix
