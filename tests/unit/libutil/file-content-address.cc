#include <gtest/gtest.h>

#include "file-content-address.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * parseFileIngestionMethod, renderFileIngestionMethod
 * --------------------------------------------------------------------------*/

TEST(FileIngestionMethod, testRoundTripPrintParse_1) {
    for (const FileIngestionMethod fim : {
        FileIngestionMethod::Flat,
        FileIngestionMethod::Recursive,
    }) {
        EXPECT_EQ(parseFileIngestionMethod(renderFileIngestionMethod(fim)), fim);
    }
}

TEST(FileIngestionMethod, testRoundTripPrintParse_2) {
    for (const std::string_view fimS : {
        "flat",
        "nar",
    }) {
        EXPECT_EQ(renderFileIngestionMethod(parseFileIngestionMethod(fimS)), fimS);
    }
}

TEST(FileIngestionMethod, testParseFileIngestionMethodOptException) {
    EXPECT_THROW(parseFileIngestionMethod("narwhal"), UsageError);
}

}
