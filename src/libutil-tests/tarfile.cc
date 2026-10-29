#include "nix/util/tarfile.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/config.hh"

#include <boost/context/detail/exception.hpp>

#include <gtest/gtest.h>

namespace nix {

#if !NIX_ASAN_ENABLED

TEST(TarArchive, forcedUnwindInsideCoroutine)
{
    int uncaughtExceptions = 42;
    bool caught = false;

    /* Models roughly what happens in makeDecompressionSink, where a TarArchive
       is created a coroutine stack and data is pulled via context switches. */
    auto sink = sourceToSink([&](Source & source) {
        auto recordUncaughtExceptions = Finally([&]() { uncaughtExceptions = std::uncaught_exceptions(); });
        try {
            TarArchive archive(source, /*raw=*/true);
            NullTarSink sink;
            unpackTarfileToSink(archive, sink);
        } catch (const boost::context::detail::forced_unwind &) {
            caught = true;
            throw;
        }
    });

    *sink << "hello there";
    // Abandon the coroutine without calling finish().
    // This will trigger it to unwind with boost::context::detail::forced_unwind.
    // The exception will be thrown inside a libarchive read callback, which we should
    // propagate outwards.
    sink.reset();

    ASSERT_TRUE(caught);
    ASSERT_EQ(uncaughtExceptions, 1);
}

#endif

TEST(TarArchive, runOnCoroutine)
{
    struct RegularFileTarSink : TarSink
    {
        StringSink sink;

        void createDirectory(const CanonPath & path) override
        {
            unreachable();
        }

        void createRegularFile(const CanonPath & path, bool isExecutable, fun<void(Sink &)> callback) override
        {
            callback(sink);
        }

        void createSymlink(const CanonPath & path, const std::string & target) override
        {
            unreachable();
        }

        void createHardlink(const CanonPath & path, const CanonPath & target) override
        {
            unreachable();
        }
    };

    std::string res;

    auto sink = sourceToSink([&](Source & source) {
        TarArchive archive(source, /*raw=*/true);
        RegularFileTarSink sink;
        unpackTarfileToSink(archive, sink);
        res = std::move(sink.sink.s);
    });

    (*sink)("hello");
    (*sink)(" there");
    sink->finish();

    ASSERT_EQ(res, "hello there");
}

TEST(TarArchive, ReadError)
{
    Pipe pipe;
    pipe.create();
    pipe.readSide.close();
    FdSource source(pipe.readSide.get());
    /* Yes, the source will be read in the constructor. */
    EXPECT_THROW({ TarArchive archive(source); }, SystemError);
}

} // namespace nix
