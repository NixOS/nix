#include <gtest/gtest.h>

#include "flake/flakeref.hh"

namespace nix {

/* ----------- tests for flake/flakeref.hh --------------------------------------------------*/

    /* ----------------------------------------------------------------------------
     * to_string
     * --------------------------------------------------------------------------*/

    TEST(to_string, doesntReencodeUrl) {
        auto s = "http://localhost:8181/test/+3d.tar.gz";
        auto flakeref = parseFlakeRef(s);
        auto parsed = flakeref.to_string();
        auto expected = "http://localhost:8181/test/%2B3d.tar.gz";

        ASSERT_EQ(parsed, expected);
    }

}
