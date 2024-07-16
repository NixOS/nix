#include <gtest/gtest.h>

#include "strings.hh"

namespace nix {

using Strings = std::vector<std::string>;

/* ----------------------------------------------------------------------------
 * concatStringsSep
 * --------------------------------------------------------------------------*/

TEST(concatStringsSep, empty)
{
    Strings strings;

    ASSERT_EQ(concatStringsSep(",", strings), "");
}

TEST(concatStringsSep, justOne)
{
    Strings strings;
    strings.push_back("this");

    ASSERT_EQ(concatStringsSep(",", strings), "this");
}

TEST(concatStringsSep, emptyString)
{
    Strings strings;
    strings.push_back("");

    ASSERT_EQ(concatStringsSep(",", strings), "");
}

TEST(concatStringsSep, emptyStrings)
{
    Strings strings;
    strings.push_back("");
    strings.push_back("");

    ASSERT_EQ(concatStringsSep(",", strings), ",");
}

TEST(concatStringsSep, threeEmptyStrings)
{
    Strings strings;
    strings.push_back("");
    strings.push_back("");
    strings.push_back("");

    ASSERT_EQ(concatStringsSep(",", strings), ",,");
}

TEST(concatStringsSep, buildCommaSeparatedString)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("is");
    strings.push_back("great");

    ASSERT_EQ(concatStringsSep(",", strings), "this,is,great");
}

TEST(concatStringsSep, buildStringWithEmptySeparator)
{
    Strings strings;
    strings.push_back("this");
    strings.push_back("is");
    strings.push_back("great");

    ASSERT_EQ(concatStringsSep("", strings), "thisisgreat");
}

TEST(concatStringsSep, buildSingleString)
{
    Strings strings;
    strings.push_back("this");

    ASSERT_EQ(concatStringsSep(",", strings), "this");
}

} // namespace nix
