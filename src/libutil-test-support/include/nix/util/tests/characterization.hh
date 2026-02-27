#pragma once
///@file

#include <gtest/gtest.h>

#include "nix/util/types.hh"
#include "nix/util/file-system.hh"
#include "nix/util/tests/test-data.hh"

namespace nix {

/**
 * Whether we should update "golden masters" instead of running tests
 * against them. See the contributing guide in the manual for further
 * details.
 */
static inline bool testAccept()
{
    return getEnv("_NIX_TEST_ACCEPT") == "1";
}

/**
 * Mixin class for writing characterization tests
 */
struct CharacterizationTest : virtual ::testing::Test
{
    /**
     * While the "golden master" for this characterization test is
     * located. It should not be shared with any other test.
     */
    virtual std::filesystem::path goldenMaster(std::string_view testStem) const = 0;

    /**
     * Golden test for reading
     *
     * @param test hook that takes the contents of the file and does the
     * actual work
     */
    void readTest(std::string_view testStem, auto && test)
    {
        auto file = goldenMaster(testStem);

        if (testAccept()) {
            GTEST_SKIP() << "Cannot read golden master " << file << "because another test is also updating it";
        } else {
            test(readFile(file));
        }
    }

    /**
     * Golden test for writing
     *
     * @param test hook that produces contents of the file and does the
     * actual work
     */
    void writeTest(std::string_view testStem, auto && test, auto && readFile2, auto && writeFile2)
    {
        auto file = goldenMaster(testStem);

        auto got = test();

        if (testAccept()) {
            std::filesystem::create_directories(file.parent_path());
            writeFile2(file, got);
            GTEST_SKIP() << "Updating golden master " << file;
        } else {
            decltype(got) expected = readFile2(file);
            ASSERT_EQ(got, expected);
        }
    }

    /**
     * Specialize to `std::string`
     */
    void writeTest(std::string_view testStem, auto && test)
    {
        writeTest(
            testStem,
            test,
            [](const std::filesystem::path & f) -> std::string { return readFile(f); },
            [](const std::filesystem::path & f, const std::string & c) { return writeFile(f, c); });
    }
};

} // namespace nix
