#include "nix/store/machines.hh"
#include "nix/util/file-system.hh"
#include "nix/util/util.hh"

#include "nix/util/tests/characterization.hh"

#include <gtest/gtest.h>
#include <gmock/gmock-matchers.h>

using testing::Contains;
using testing::ElementsAre;
using testing::Eq;
using testing::Field;
using testing::SizeIs;

using namespace nix;

TEST(machines, getMachinesWithEmptyBuilders)
{
    auto actual = Machine::parseConfig({}, "");
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesUriOnly)
{
    auto actual = Machine::parseConfig({"TEST_ARCH-TEST_OS"}, "nix@scratchy.labs.cs.uu.nl");
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, Eq(StoreReference::parse("ssh://nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("TEST_ARCH-TEST_OS")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, SizeIs(0)));
}

TEST(machines, getMachinesUriWithPort)
{
    auto actual = Machine::parseConfig({"TEST_ARCH-TEST_OS"}, "nix@scratchy.labs.cs.uu.nl:2222");
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(
        actual[0], Field(&Machine::storeUri, Eq(StoreReference::parse("ssh://nix@scratchy.labs.cs.uu.nl:2222"))));
}

TEST(machines, getMachinesDefaults)
{
    auto actual = Machine::parseConfig({"TEST_ARCH-TEST_OS"}, "nix@scratchy.labs.cs.uu.nl - - - - - - -");
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, Eq(StoreReference::parse("ssh://nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("TEST_ARCH-TEST_OS")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, SizeIs(0)));
}

MATCHER_P(AuthorityMatches, authority, "")
{
    *result_listener << "where the authority of " << arg.render() << " is " << authority;
    auto * generic = std::get_if<StoreReference::Specified>(&arg.variant);
    if (!generic)
        return false;
    return generic->authority == authority;
}

TEST(machines, getMachinesWithNewLineSeparator)
{
    auto actual = Machine::parseConfig({}, "nix@scratchy.labs.cs.uu.nl\nnix@itchy.labs.cs.uu.nl");
    ASSERT_THAT(actual, SizeIs(2));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@itchy.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithSemicolonSeparator)
{
    auto actual = Machine::parseConfig({}, "nix@scratchy.labs.cs.uu.nl ; nix@itchy.labs.cs.uu.nl");
    EXPECT_THAT(actual, SizeIs(2));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@itchy.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithCommentsAndSemicolonSeparator)
{
    auto actual = Machine::parseConfig(
        {},
        "# This is a comment ; this is still that comment\n"
        "nix@scratchy.labs.cs.uu.nl ; nix@itchy.labs.cs.uu.nl\n"
        "# This is also a comment ; this also is still that comment\n"
        "nix@scabby.labs.cs.uu.nl\n");
    EXPECT_THAT(actual, SizeIs(3));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@itchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@scabby.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithFunnyWhitespace)
{
    auto actual = Machine::parseConfig(
        {},
        "        # comment ; comment\n"
        "   nix@scratchy.labs.cs.uu.nl ; nix@itchy.labs.cs.uu.nl   \n"
        "\n    \n"
        "\n ;;; \n"
        "\n ; ; \n"
        "nix@scabby.labs.cs.uu.nl\n\n");
    EXPECT_THAT(actual, SizeIs(3));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@itchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@scabby.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithCorrectCompleteSingleBuilder)
{
    auto actual = Machine::parseConfig(
        {},
        "nix@scratchy.labs.cs.uu.nl     i686-linux      "
        "/home/nix/.ssh/id_scratchy_auto        8 3 kvm "
        "benchmark SSH+HOST+PUBLIC+KEY+BASE64+ENCODED==");
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("i686-linux")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, Eq("/home/nix/.ssh/id_scratchy_auto")));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(8)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("kvm")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("benchmark")));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, Eq("SSH+HOST+PUBLIC+KEY+BASE64+ENCODED==")));
}

TEST(machines, getMachinesWithCorrectCompleteSingleBuilderWithTabColumnDelimiter)
{
    auto actual = Machine::parseConfig(
        {},
        "nix@scratchy.labs.cs.uu.nl\ti686-linux\t/home/nix/.ssh/"
        "id_scratchy_auto\t8\t3\tkvm\tbenchmark\tSSH+HOST+PUBLIC+"
        "KEY+BASE64+ENCODED==");
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("i686-linux")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, Eq("/home/nix/.ssh/id_scratchy_auto")));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(8)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("kvm")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("benchmark")));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, Eq("SSH+HOST+PUBLIC+KEY+BASE64+ENCODED==")));
}

TEST(machines, getMachinesWithMultiOptions)
{
    auto actual = Machine::parseConfig(
        {},
        "nix@scratchy.labs.cs.uu.nl Arch1,Arch2 - - - "
        "SupportedFeature1,SupportedFeature2 "
        "MandatoryFeature1,MandatoryFeature2");
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("Arch1", "Arch2")));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("SupportedFeature1", "SupportedFeature2")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("MandatoryFeature1", "MandatoryFeature2")));
}

TEST(machines, getMachinesWithIncorrectFormat)
{
    EXPECT_THROW(Machine::parseConfig({}, "nix@scratchy.labs.cs.uu.nl - - eight"), FormatError);
    EXPECT_THROW(Machine::parseConfig({}, "nix@scratchy.labs.cs.uu.nl - - -1"), FormatError);
    EXPECT_THROW(Machine::parseConfig({}, "nix@scratchy.labs.cs.uu.nl - - 8 three"), FormatError);
    EXPECT_THROW(Machine::parseConfig({}, "nix@scratchy.labs.cs.uu.nl - - 8 -3"), UsageError);
    EXPECT_THROW(Machine::parseConfig({}, "nix@scratchy.labs.cs.uu.nl - - 8 3 - - BAD_BASE64"), FormatError);
}

TEST(machines, getMachinesWithCorrectFileReference)
{
    auto path = std::filesystem::weakly_canonical(getUnitTestData() / "machines/valid");
    ASSERT_TRUE(std::filesystem::exists(path));

    auto actual = Machine::parseConfig({}, "@" + path.string());
    ASSERT_THAT(actual, SizeIs(3));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@itchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, AuthorityMatches("nix@poochie.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithCorrectFileReferenceToEmptyFile)
{
    std::filesystem::path path = "/dev/null";
    ASSERT_TRUE(std::filesystem::exists(path));

    auto actual = Machine::parseConfig({}, "@" + path.string());
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesWithIncorrectFileReference)
{
    auto path = std::filesystem::weakly_canonical("/not/a/file");
    ASSERT_TRUE(!std::filesystem::exists(path));
    auto actual = Machine::parseConfig({}, "@" + path.string());
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesWithCorrectFileReferenceToIncorrectFile)
{
    EXPECT_THROW(
        Machine::parseConfig(
            {}, "@" + std::filesystem::weakly_canonical(getUnitTestData() / "machines" / "bad_format").string()),
        FormatError);
}
