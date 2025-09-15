#include <gtest/gtest.h>

#include "nix/store/content-address.hh"

namespace nix {

/* ----------------------------------------------------------------------------
 * ContentAddressMethod::parse, ContentAddressMethod::render
 * --------------------------------------------------------------------------*/

TEST(ContentAddressMethod, testRoundTripPrintParse_1)
{
    for (ContentAddressMethod cam : {
             ContentAddressMethod::Raw::Text,
             ContentAddressMethod::Raw::Flat,
             ContentAddressMethod::Raw::NixArchive,
             ContentAddressMethod::Raw::Git,
         }) {
        EXPECT_EQ(ContentAddressMethod::parse(cam.render()), cam);
    }
}

TEST(ContentAddressMethod, testRoundTripPrintParse_2)
{
    for (const std::string_view camS : {
             "text",
             "flat",
             "nar",
             "git",
         }) {
        EXPECT_EQ(ContentAddressMethod::parse(camS).render(), camS);
    }
}

TEST(ContentAddressMethod, testParseContentAddressMethodOptException)
{
    EXPECT_THROW(ContentAddressMethod::parse("narwhal"), UsageError);
}

} // namespace nix
