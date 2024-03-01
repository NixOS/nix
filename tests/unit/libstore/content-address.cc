#include <gtest/gtest.h>

#include "content-address.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * ContentAddressMethod::parse, ContentAddressMethod::render
 * --------------------------------------------------------------------------*/

TEST(ContentAddressMethod, testRoundTripPrintParse_1) {
    for (const ContentAddressMethod & cam : {
        ContentAddressMethod { TextIngestionMethod {} },
        ContentAddressMethod { FileIngestionMethod::Flat },
        ContentAddressMethod { FileIngestionMethod::Recursive },
        ContentAddressMethod { FileIngestionMethod::Git },
    }) {
        EXPECT_EQ(ContentAddressMethod::parse(cam.render()), cam);
    }
}

TEST(ContentAddressMethod, testRoundTripPrintParse_2) {
    for (const std::string_view camS : {
        "text",
        "flat",
        "nar",
        "git",
    }) {
        EXPECT_EQ(ContentAddressMethod::parse(camS).render(), camS);
    }
}

TEST(ContentAddressMethod, testParseContentAddressMethodOptException) {
    EXPECT_THROW(ContentAddressMethod::parse("narwhal"), UsageError);
}

}
