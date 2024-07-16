#if ENABLE_S3

#  include <gtest/gtest.h>

#  include "s3-binary-cache-store.hh"

namespace nix {

TEST(S3BinaryCacheStore, constructConfig)
{
    S3BinaryCacheStoreConfig config{"s3", "foobar", {}};

    EXPECT_EQ(config.bucketName, "foobar");
}

} // namespace nix

#endif
