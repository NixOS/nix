#include "nix/util/serialise.hh"

#include <boost/context/detail/exception.hpp>
#include <gtest/gtest.h>

namespace nix {

template<typename T>
    requires std::is_integral_v<T>
auto makeNumSource(T num)
{
    return sinkToSource([num](Sink & writer) { writer << num; });
}

TEST(readNum, negativeValuesSerialiseWellDefined)
{
    EXPECT_THROW(readNum<uint32_t>(*makeNumSource(int64_t(-1))), SerialisationError);
    EXPECT_THROW(readNum<int64_t>(*makeNumSource(int16_t(-1))), SerialisationError);
    EXPECT_EQ(readNum<uint64_t>(*makeNumSource(int64_t(-1))), std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(readNum<uint64_t>(*makeNumSource(int64_t(-2))), std::numeric_limits<uint64_t>::max() - 1);
    /* The result doesn't depend on the source type - only the destination matters. */
    EXPECT_EQ(readNum<uint64_t>(*makeNumSource(int32_t(-1))), std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(readNum<uint64_t>(*makeNumSource(int16_t(-1))), std::numeric_limits<uint64_t>::max());
}

TEST(readPadding, works)
{
    for (unsigned i = 0; i < 8; ++i)
        EXPECT_NO_THROW(readPadding(i, *makeNumSource(0)));

    for (unsigned len = 1; len < 8; ++len) {
        EXPECT_THROW(readPadding(len, *makeNumSource(~uint64_t(0))), SerialisationError);
        unsigned padLen = 8 - len;
        for (unsigned byte = 0; byte < padLen; ++byte) {
            uint64_t val = uint64_t{1} << (8 * byte);
            EXPECT_THROW(readPadding(len, *makeNumSource(val)), SerialisationError);
        }
    }
}

TEST(sourceToSink, forcedUnwindUcaughtExceptions)
{
    int uncaughtExceptions = 42;
    bool caught = false;

    auto sink = sourceToSink([&](Source & source) {
        auto recordUncaughtExceptions = Finally([&]() { uncaughtExceptions = std::uncaught_exceptions(); });
        try {
            StringSink s;
            source.drainInto(s, 8);
            source.drainInto(s, 8);
        } catch (const boost::context::detail::forced_unwind &) {
            caught = true;
            throw;
        }
    });

    *sink << 42;

    // Abandon the coroutine. This will trigger it to unwind with boost::context::detail::forced_unwind.
    sink.reset();

    ASSERT_TRUE(caught);
    // This is a test for boost.context regression fixed by https://github.com/boostorg/context/pull/337.
    // Without the fix std::uncaught_exceptions() *misreports* 0 while there's stack unwinding in progress.
    // The issue only surfaces with libstdc++ and fcontext when boost.context
    // uses fiber-specific exception states and messes with __cxa_get_globals().
    ASSERT_EQ(uncaughtExceptions, 0);
}

} // namespace nix
