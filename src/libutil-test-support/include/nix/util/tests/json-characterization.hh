#pragma once
///@file

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/util/types.hh"
#include "nix/util/file-system.hh"

#include "nix/util/tests/characterization.hh"

namespace nix {

/**
 * Golden test for JSON reading
 */
template<typename T>
void readJsonTest(CharacterizationTest & test, PathView testStem, const T & expected, auto... args)
{
    using namespace nlohmann;
    test.readTest(Path{testStem} + ".json", [&](const auto & encodedRaw) {
        auto encoded = json::parse(encodedRaw);
        T decoded = adl_serializer<T>::from_json(encoded, args...);
        ASSERT_EQ(decoded, expected);
    });
}

/**
 * Golden test for JSON writing
 */
template<typename T>
void writeJsonTest(CharacterizationTest & test, PathView testStem, const T & value)
{
    using namespace nlohmann;
    test.writeTest(
        Path{testStem} + ".json",
        [&]() -> json { return static_cast<json>(value); },
        [](const auto & file) { return json::parse(readFile(file)); },
        [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });
}

/**
 * Mixin class for writing characterization tests for `nlohmann::json`
 * conversions for a given type.
 */
template<typename T>
struct JsonCharacterizationTest : virtual CharacterizationTest
{
    /**
     * Golden test for reading
     *
     * @param test hook that takes the contents of the file and does the
     * actual work
     */
    void readJsonTest(PathView testStem, const T & expected, auto... args)
    {
        nix::readJsonTest(*this, testStem, expected, args...);
    }

    /**
     * Golden test for writing
     *
     * @param test hook that produces contents of the file and does the
     * actual work
     */
    void writeJsonTest(PathView testStem, const T & value)
    {
        nix::writeJsonTest(*this, testStem, value);
    }
};

} // namespace nix
