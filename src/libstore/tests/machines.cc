#include "machines.hh"
#include "globals.hh"

#include <gmock/gmock-matchers.h>

using testing::Contains;
using testing::ElementsAre;
using testing::EndsWith;
using testing::Eq;
using testing::Field;
using testing::SizeIs;

using nix::absPath;
using nix::FormatError;
using nix::getMachines;
using nix::Machine;
using nix::Machines;
using nix::pathExists;
using nix::Settings;
using nix::settings;

class Environment : public ::testing::Environment {
  public:
    void SetUp() override { settings.thisSystem = "TEST_ARCH-TEST_OS"; }
};

testing::Environment* const foo_env =
    testing::AddGlobalTestEnvironment(new Environment);

TEST(machines, getMachinesWithEmptyBuilders) {
    settings.builders = "";
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesUriOnly) {
    settings.builders = "nix@scratchy.labs.cs.uu.nl";
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, Eq("ssh://nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("TEST_ARCH-TEST_OS")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, SizeIs(0)));
}

TEST(machines, getMachinesDefaults) {
    settings.builders = "nix@scratchy.labs.cs.uu.nl - - - - - - -";
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, Eq("ssh://nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("TEST_ARCH-TEST_OS")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(1)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, SizeIs(0)));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, SizeIs(0)));
}

TEST(machines, getMachinesWithNewLineSeparator) {
    settings.builders = "nix@scratchy.labs.cs.uu.nl\nnix@itchy.labs.cs.uu.nl";
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(2));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@itchy.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithSemicolonSeparator) {
    settings.builders = "nix@scratchy.labs.cs.uu.nl ; nix@itchy.labs.cs.uu.nl";
    Machines actual = getMachines();
    EXPECT_THAT(actual, SizeIs(2));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@itchy.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithCorrectCompleteSingleBuilder) {
    settings.builders = "nix@scratchy.labs.cs.uu.nl     i686-linux      "
                        "/home/nix/.ssh/id_scratchy_auto        8 3 kvm "
                        "benchmark SSH+HOST+PUBLIC+KEY+BASE64+ENCODED==";
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("i686-linux")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, Eq("/home/nix/.ssh/id_scratchy_auto")));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(8)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("kvm")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("benchmark")));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, Eq("SSH+HOST+PUBLIC+KEY+BASE64+ENCODED==")));
}

TEST(machines,
     getMachinesWithCorrectCompleteSingleBuilderWithTabColumnDelimiter) {
    settings.builders =
        "nix@scratchy.labs.cs.uu.nl\ti686-linux\t/home/nix/.ssh/"
        "id_scratchy_auto\t8\t3\tkvm\tbenchmark\tSSH+HOST+PUBLIC+"
        "KEY+BASE64+ENCODED==";
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("i686-linux")));
    EXPECT_THAT(actual[0], Field(&Machine::sshKey, Eq("/home/nix/.ssh/id_scratchy_auto")));
    EXPECT_THAT(actual[0], Field(&Machine::maxJobs, Eq(8)));
    EXPECT_THAT(actual[0], Field(&Machine::speedFactor, Eq(3)));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("kvm")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("benchmark")));
    EXPECT_THAT(actual[0], Field(&Machine::sshPublicHostKey, Eq("SSH+HOST+PUBLIC+KEY+BASE64+ENCODED==")));
}

TEST(machines, getMachinesWithMultiOptions) {
    settings.builders = "nix@scratchy.labs.cs.uu.nl Arch1,Arch2 - - - "
                        "SupportedFeature1,SupportedFeature2 "
                        "MandatoryFeature1,MandatoryFeature2";
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(1));
    EXPECT_THAT(actual[0], Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl")));
    EXPECT_THAT(actual[0], Field(&Machine::systemTypes, ElementsAre("Arch1", "Arch2")));
    EXPECT_THAT(actual[0], Field(&Machine::supportedFeatures, ElementsAre("SupportedFeature1", "SupportedFeature2")));
    EXPECT_THAT(actual[0], Field(&Machine::mandatoryFeatures, ElementsAre("MandatoryFeature1", "MandatoryFeature2")));
}

TEST(machines, getMachinesWithIncorrectFormat) {
    settings.builders = "nix@scratchy.labs.cs.uu.nl - - eight";
    EXPECT_THROW(getMachines(), FormatError);
    settings.builders = "nix@scratchy.labs.cs.uu.nl - - -1";
    EXPECT_THROW(getMachines(), FormatError);
    settings.builders = "nix@scratchy.labs.cs.uu.nl - - 8 three";
    EXPECT_THROW(getMachines(), FormatError);
    settings.builders = "nix@scratchy.labs.cs.uu.nl - - 8 -3";
    EXPECT_THROW(getMachines(), FormatError);
    settings.builders = "nix@scratchy.labs.cs.uu.nl - - 8 3 - - BAD_BASE64";
    EXPECT_THROW(getMachines(), FormatError);
}

TEST(machines, getMachinesWithCorrectFileReference) {
    auto path = absPath("src/libstore/tests/test-data/machines.valid");
    ASSERT_TRUE(pathExists(path));

    settings.builders = std::string("@") + path;
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(3));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@scratchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@itchy.labs.cs.uu.nl"))));
    EXPECT_THAT(actual, Contains(Field(&Machine::storeUri, EndsWith("nix@poochie.labs.cs.uu.nl"))));
}

TEST(machines, getMachinesWithCorrectFileReferenceToEmptyFile) {
    auto path = "/dev/null";
    ASSERT_TRUE(pathExists(path));

    settings.builders = std::string("@") + path;
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesWithIncorrectFileReference) {
    settings.builders = std::string("@") + absPath("/not/a/file");
    Machines actual = getMachines();
    ASSERT_THAT(actual, SizeIs(0));
}

TEST(machines, getMachinesWithCorrectFileReferenceToIncorrectFile) {
    settings.builders = std::string("@") + absPath("src/libstore/tests/test-data/machines.bad_format");
    EXPECT_THROW(getMachines(), FormatError);
}
