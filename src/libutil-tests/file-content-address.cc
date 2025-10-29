#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/util/file-content-address.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * parseFileSerialisationMethod, renderFileSerialisationMethod
 * --------------------------------------------------------------------------*/

TEST(FileSerialisationMethod, testRoundTripPrintParse_1)
{
    for (const FileSerialisationMethod fim : {
             FileSerialisationMethod::Flat,
             FileSerialisationMethod::NixArchive,
         }) {
        EXPECT_EQ(parseFileSerialisationMethod(renderFileSerialisationMethod(fim)), fim);
    }
}

TEST(FileSerialisationMethod, testRoundTripPrintParse_2)
{
    for (const std::string_view fimS : {
             "flat",
             "nar",
         }) {
        EXPECT_EQ(renderFileSerialisationMethod(parseFileSerialisationMethod(fimS)), fimS);
    }
}

TEST(FileSerialisationMethod, testParseFileSerialisationMethodOptException)
{
    EXPECT_THAT(
        []() { parseFileSerialisationMethod("narwhal"); },
        testing::ThrowsMessage<UsageError>(testing::HasSubstr("narwhal")));
}

/* ----------------------------------------------------------------------------
 * parseFileIngestionMethod, renderFileIngestionMethod
 * --------------------------------------------------------------------------*/

TEST(FileIngestionMethod, testRoundTripPrintParse_1)
{
    for (const FileIngestionMethod fim : {
             FileIngestionMethod::Flat,
             FileIngestionMethod::NixArchive,
             FileIngestionMethod::Git,
         }) {
        EXPECT_EQ(parseFileIngestionMethod(renderFileIngestionMethod(fim)), fim);
    }
}

TEST(FileIngestionMethod, testRoundTripPrintParse_2)
{
    for (const std::string_view fimS : {
             "flat",
             "nar",
             "git",
         }) {
        EXPECT_EQ(renderFileIngestionMethod(parseFileIngestionMethod(fimS)), fimS);
    }
}

TEST(FileIngestionMethod, testParseFileIngestionMethodOptException)
{
    EXPECT_THAT(
        []() { parseFileIngestionMethod("narwhal"); },
        testing::ThrowsMessage<UsageError>(testing::HasSubstr("narwhal")));
}

} // namespace nix
