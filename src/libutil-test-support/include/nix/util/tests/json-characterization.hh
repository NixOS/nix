#pragma once
///@file

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/util/types.hh"
#include "nix/util/ref.hh"
#include "nix/util/file-system.hh"

#include "nix/util/tests/characterization.hh"

namespace nix {

/**
 * Golden test for JSON reading
 */
template<typename T>
void readJsonTest(CharacterizationTest & test, std::string_view testStem, const T & expected, auto... args)
{
    using namespace nlohmann;
    test.readTest(std::string{testStem} + ".json", [&](const auto & encodedRaw) {
        auto encoded = json::parse(encodedRaw);
        T decoded = adl_serializer<T>::from_json(encoded, args...);
        ASSERT_EQ(decoded, expected);
    });
}

/**
 * Golden test for JSON writing
 */
template<typename T>
void writeJsonTest(CharacterizationTest & test, std::string_view testStem, const T & value)
{
    using namespace nlohmann;
    test.writeTest(
        std::string{testStem} + ".json",
        [&]() -> json { return static_cast<json>(value); },
        [](const auto & file) { return json::parse(readFile(file)); },
        [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });
}

/**
 * Specialization for when we need to do "JSON -> `ref<T>`" in one
 * direction, but "`const T &` -> JSON" in the other direction.
 *
 * We can't just return `const T &`, but it would be wasteful to
 * requires a `const ref<T> &` double indirection (and mandatory shared
 * pointer), so we break the symmetry as the best remaining option.
 */
template<typename T>
void writeJsonTest(CharacterizationTest & test, std::string_view testStem, const ref<T> & value)
{
    using namespace nlohmann;
    test.writeTest(
        std::string{testStem} + ".json",
        [&]() -> json { return static_cast<json>(*value); },
        [](const auto & file) { return json::parse(readFile(file)); },
        [](const auto & file, const auto & got) { return writeFile(file, got.dump(2) + "\n"); });
}

/**
 * Golden test in the middle of something
 */
template<typename T>
void checkpointJson(CharacterizationTest & test, std::string_view testStem, const T & got)
{
    using namespace nlohmann;

    auto file = test.goldenMaster(std::string{testStem} + ".json");

    json gotJson = static_cast<json>(got);

    if (testAccept()) {
        std::filesystem::create_directories(file.parent_path());
        writeFile(file, gotJson.dump(2) + "\n");
        ADD_FAILURE() << "Updating golden master " << file;
    } else {
        json expectedJson = json::parse(readFile(file));
        ASSERT_EQ(gotJson, expectedJson);
        T expected = adl_serializer<T>::from_json(expectedJson);
        ASSERT_EQ(got, expected);
    }
}

/**
 * Specialization for when we need to do "JSON -> `ref<T>`" in one
 * direction, but "`const T &` -> JSON" in the other direction.
 */
template<typename T>
void checkpointJson(CharacterizationTest & test, std::string_view testStem, const ref<T> & got)
{
    using namespace nlohmann;

    auto file = test.goldenMaster(std::string{testStem} + ".json");

    json gotJson = static_cast<json>(*got);

    if (testAccept()) {
        std::filesystem::create_directories(file.parent_path());
        writeFile(file, gotJson.dump(2) + "\n");
        ADD_FAILURE() << "Updating golden master " << file;
    } else {
        json expectedJson = json::parse(readFile(file));
        ASSERT_EQ(gotJson, expectedJson);
        ref<T> expected = adl_serializer<ref<T>>::from_json(expectedJson);
        ASSERT_EQ(*got, *expected);
    }
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
    void readJsonTest(std::string_view testStem, const T & expected, auto... args)
    {
        nix::readJsonTest(*this, testStem, expected, args...);
    }

    /**
     * Golden test for writing
     *
     * @param test hook that produces contents of the file and does the
     * actual work
     */
    void writeJsonTest(std::string_view testStem, const T & value)
    {
        nix::writeJsonTest(*this, testStem, value);
    }

    void checkpointJson(std::string_view testStem, const T & value)
    {
        nix::checkpointJson(*this, testStem, value);
    }
};

} // namespace nix
