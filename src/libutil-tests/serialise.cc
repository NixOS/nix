#include "nix/util/serialise.hh"

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

} // namespace nix
