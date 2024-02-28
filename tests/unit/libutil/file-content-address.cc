#include <gtest/gtest.h>

#include "file-content-address.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * parseFileSerialisationMethod, renderFileSerialisationMethod
 * --------------------------------------------------------------------------*/

TEST(FileSerialisationMethod, testRoundTripPrintParse_1) {
    for (const FileSerialisationMethod fim : {
        FileSerialisationMethod::Flat,
        FileSerialisationMethod::Recursive,
    }) {
        EXPECT_EQ(parseFileSerialisationMethod(renderFileSerialisationMethod(fim)), fim);
    }
}

TEST(FileSerialisationMethod, testRoundTripPrintParse_2) {
    for (const std::string_view fimS : {
        "flat",
        "nar",
    }) {
        EXPECT_EQ(renderFileSerialisationMethod(parseFileSerialisationMethod(fimS)), fimS);
    }
}

TEST(FileSerialisationMethod, testParseFileSerialisationMethodOptException) {
    EXPECT_THROW(parseFileSerialisationMethod("narwhal"), UsageError);
}

/* ----------------------------------------------------------------------------
 * parseFileIngestionMethod, renderFileIngestionMethod
 * --------------------------------------------------------------------------*/

TEST(FileIngestionMethod, testRoundTripPrintParse_1) {
    for (const FileIngestionMethod fim : {
        FileIngestionMethod::Flat,
        FileIngestionMethod::Recursive,
        FileIngestionMethod::Git,
    }) {
        EXPECT_EQ(parseFileIngestionMethod(renderFileIngestionMethod(fim)), fim);
    }
}

TEST(FileIngestionMethod, testRoundTripPrintParse_2) {
    for (const std::string_view fimS : {
        "flat",
        "nar",
        "git",
    }) {
        EXPECT_EQ(renderFileIngestionMethod(parseFileIngestionMethod(fimS)), fimS);
    }
}

TEST(FileIngestionMethod, testParseFileIngestionMethodOptException) {
    EXPECT_THROW(parseFileIngestionMethod("narwhal"), UsageError);
}

}
