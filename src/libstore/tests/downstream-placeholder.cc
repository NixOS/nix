#include <gtest/gtest.h>

#include "derivations.hh"

namespace nix {

TEST(Derivation, downstreamPlaceholder) {
    ASSERT_EQ(
        downstreamPlaceholder(
            (const Store &)*(const Store *)nullptr, // argument is unused
            StorePath { "g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-foo.drv" },
            "out"),
        "/0c6rn30q4frawknapgwq386zq358m8r6msvywcvc89n6m5p2dgbz");
}

}
