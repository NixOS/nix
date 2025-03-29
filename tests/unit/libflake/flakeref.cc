#include <gtest/gtest.h>

#include "fetch-settings.hh"
#include "flake/flakeref.hh"

namespace nix {

/* ----------- tests for flake/flakeref.hh --------------------------------------------------*/

    /* ----------------------------------------------------------------------------
     * to_string
     * --------------------------------------------------------------------------*/

    TEST(to_string, doesntReencodeUrl) {
        fetchers::Settings fetchSettings;
        auto s = "http://localhost:8181/test/+3d.tar.gz";
        auto flakeref = parseFlakeRef(fetchSettings, s);
        auto parsed = flakeref.to_string();
        auto expected = "http://localhost:8181/test/%2B3d.tar.gz";

        ASSERT_EQ(parsed, expected);
    }

    /* ----------------------------------------------------------------------------
     * parseFlakeRef
     * --------------------------------------------------------------------------*/

    TEST(parseFlakeRef, removesDirFromInputURL) {
        fetchers::Settings fetchSettings;
        auto s = "git+https://localhost:8181/test/test.git?dir=subdir";
        auto flakeref = parseFlakeRef(fetchSettings, s);
        auto expected = "git+https://localhost:8181/test/test.git";
        auto inputURL = flakeref.input.toURLString();

        ASSERT_EQ(inputURL, expected);
    }

    TEST(parseFlakeRef, setsSubdir) {
        fetchers::Settings fetchSettings;
        auto s = "git+https://localhost:8181/test/test.git?dir=subdir";
        auto flakeref = parseFlakeRef(fetchSettings, s);
        auto expected = "subdir";
        auto flakerefSubdir = flakeref.subdir;

        ASSERT_EQ(flakerefSubdir, expected);
    }
}
