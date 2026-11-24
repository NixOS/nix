#include "nix/util/tarfile.hh"
#include "nix/util/canon-path.hh"
#include "nix/util/serialise.hh"
#include "nix/util/file-descriptor.hh"
#include "nix/util/config.hh"
#include "nix/util/system-error.hh"
#include "nix/util/finally.hh"

#include <boost/context/detail/exception.hpp>

#include <format>

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
 * A tarball of one entry named @p name: a regular file with @p contents,
 * or, if @p linkTarget is given, a hard link to that. Just enough of the
 * ustar format for libarchive.
 */
static std::string
makeTarball(std::string_view name, std::string_view contents, std::optional<std::string_view> linkTarget = std::nullopt)
{
    std::string header(512, '\0');
    auto put = [&](size_t offset, std::string_view s) { std::ranges::copy(s, header.begin() + offset); };
    auto size = linkTarget ? 0 : contents.size();
    put(0, name);
    put(100, "0000644");
    put(108, "0000000");
    put(116, "0000000");
    put(124, std::format("{:011o}", size));
    put(136, "00000000000");
    header[156] = linkTarget ? '1' : '0';
    if (linkTarget)
        put(157, *linkTarget);
    put(257, "ustar");
    put(263, "00");
    /* The checksum is computed with its own field as spaces. */
    put(148, "        ");
    unsigned checksum = 0;
    for (unsigned char c : header)
        checksum += c;
    put(148, std::format("{:06o}", checksum));
    header[154] = '\0';

    auto res = std::move(header);
    res += contents.substr(0, size);
    res.append((512 - size % 512) % 512, '\0');
    /* The end of the archive. */
    res.append(1024, '\0');
    return res;
}

TEST(TarArchive, rejectsDotDot)
{
    auto unpack = [](const std::string & tarball) {
        StringSource source{tarball};
        TarArchive archive(source);
        NullTarSink sink;
        unpackTarfileToSink(archive, sink);
    };

    EXPECT_NO_THROW(unpack(makeTarball("a/b", "contents")));
    EXPECT_THROW(unpack(makeTarball("a/../b", "contents")), BadCanonPath);
    EXPECT_THROW(unpack(makeTarball("../b", "contents")), BadCanonPath);
    /* The target of a hard link too. */
    EXPECT_THROW(unpack(makeTarball("b", "", "a/../c")), BadCanonPath);
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
