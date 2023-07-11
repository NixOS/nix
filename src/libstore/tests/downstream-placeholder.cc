#include <gtest/gtest.h>

#include "downstream-placeholder.hh"

namespace nix {

TEST(DownstreamPlaceholder, unknownCaOutput) {
    ASSERT_EQ(
        DownstreamPlaceholder::unknownCaOutput(
            StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv" },
            "out").render(),
        "/0c6rn30q4frawknapgwq386zq358m8r6msvywcvc89n6m5p2dgbz");
}

TEST(DownstreamPlaceholder, unknownDerivation) {
    /**
     * We set these in tests rather than the regular globals so we don't have
     * to worry about race conditions if the tests run concurrently.
     */
    ExperimentalFeatureSettings mockXpSettings;
    mockXpSettings.set("experimental-features", "dynamic-derivations ca-derivations");

    ASSERT_EQ(
        DownstreamPlaceholder::unknownDerivation(
            DownstreamPlaceholder::unknownCaOutput(
                StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv.drv" },
                "out"),
            "out",
            mockXpSettings).render(),
        "/0gn6agqxjyyalf0dpihgyf49xq5hqxgw100f0wydnj6yqrhqsb3w");
}

}
