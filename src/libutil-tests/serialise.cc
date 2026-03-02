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

} // namespace nix
