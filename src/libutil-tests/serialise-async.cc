#include "nix/util/serialise-async.hh"
#include "nix/util/archive.hh"
#include "nix/util/memory-source-accessor.hh"
#include "nix/util/tests/gmock-matchers.hh"

#include <boost/asio/use_future.hpp>

#include <gtest/gtest.h>

namespace nix {

class AsyncFdTest : public ::testing::Test
{
protected:
    asio::io_context ctx;
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

    ctx.run();

    std::unique_ptr<MemorySourceAccessor> accessor;

    ASSERT_NO_THROW(accessor = read.get());
    ASSERT_NO_THROW(write.get());
    EXPECT_THAT(accessor, ::nix::testing::HasContents(CanonPath::root, contents));
}

TEST_F(AsyncFdTest, writeToInvalidFd)
{
    Pipe pipe;
    pipe.create();

    auto write = asio::spawn(
        ctx,
        [&](asio::yield_context yield) {
            Descriptor fd = pipe.writeSide.get();
            AsyncFdSink sink(ctx, yield, std::move(pipe.writeSide));
            ::close(fd);
            sink << "hello";
            sink.flush();
        },
        asio::use_future);

    ctx.run();
    EXPECT_THROW(write.get(), SystemError);
}

} // namespace nix
