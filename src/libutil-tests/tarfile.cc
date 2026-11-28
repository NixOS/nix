#include "nix/util/tarfile.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/config.hh"
#include "nix/util/system-error.hh"
#include "nix/util/finally.hh"
#include "nix/util/file-system.hh"
#include "nix/util/tests/gmock-matchers.hh"
#include "nix/util/tests/test-data.hh"

#include <boost/context/detail/exception.hpp>

#include <map>

#include <gmock/gmock.h>
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

/**
 * Unpack the tarball @p name from the test data, made by its
 * `generate.sh`, into a `TarSink` that records the files created.
 */
static std::map<std::string, std::string> unpackTestTarball(std::string_view name)
{
    struct RecordingTarSink : TarSink
    {
        std::map<std::string, std::string> files;

        void createDirectory(const CanonPath & path) override {}

        void createRegularFile(const CanonPath & path, bool isExecutable, fun<void(Sink &)> callback) override
        {
            StringSink sink;
            callback(sink);
            files.insert_or_assign(path.abs(), std::move(sink.s));
        }

        void createSymlink(const CanonPath & path, const std::string & target) override {}

        void createHardlink(const CanonPath & path, const CanonPath & target) override
        {
            files.insert_or_assign(path.abs(), files.at(target.abs()));
        }
    };

    auto tarball = readFile(getUnitTestData() / "tarfile" / (std::string{name} + ".tar"));
    StringSource source{tarball};
    TarArchive archive(source);
    RecordingTarSink sink;
    unpackTarfileToSink(archive, sink);
    return std::move(sink.files);
}

TEST(TarArchive, rejectsDotDot)
{
    using ::testing::AllOf;
    using testing::HasSubstrIgnoreANSIMatcher;
    using ::testing::ThrowsMessage;

    EXPECT_EQ(unpackTestTarball("ok"), (std::map<std::string, std::string>{{"/a/b", "contents\n"}}));
    EXPECT_THAT(
        [] { unpackTestTarball("dotdot"); },
        ThrowsMessage<BadCanonPath>(AllOf(
            HasSubstrIgnoreANSIMatcher("must not contain '..' components"),
            HasSubstrIgnoreANSIMatcher("in archive member 'a/../b'"))));
    /* The target of a hard link too. */
    EXPECT_THAT(
        [] { unpackTestTarball("hardlink-dotdot"); },
        ThrowsMessage<BadCanonPath>(AllOf(
            HasSubstrIgnoreANSIMatcher("must not contain '..' components"),
            HasSubstrIgnoreANSIMatcher("in the target of hard link 'd'"))));
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
