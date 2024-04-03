#include <vector>
#include <optional>

#include <gtest/gtest.h>

#include "error.hh"
#include "json-utils.hh"

namespace nix {

/* Test `to_json` and `from_json` with `std::optional` types.
 * We are specifically interested in whether we can _nest_ optionals in STL
 * containers so we that we can leverage existing adl_serializer templates. */

TEST(to_json, optionalInt) {
    std::optional<int> val = std::make_optional(420);
    ASSERT_EQ(nlohmann::json(val), nlohmann::json(420));
    val = std::nullopt;
    ASSERT_EQ(nlohmann::json(val), nlohmann::json(nullptr));
}

TEST(to_json, vectorOfOptionalInts) {
    std::vector<std::optional<int>> vals = {
        std::make_optional(420),
        std::nullopt,
    };
    ASSERT_EQ(nlohmann::json(vals), nlohmann::json::parse("[420,null]"));
}

TEST(to_json, optionalVectorOfInts) {
    std::optional<std::vector<int>> val = std::make_optional(std::vector<int> {
        -420,
        420,
    });
    ASSERT_EQ(nlohmann::json(val), nlohmann::json::parse("[-420,420]"));
    val = std::nullopt;
    ASSERT_EQ(nlohmann::json(val), nlohmann::json(nullptr));
}

TEST(from_json, optionalInt) {
    nlohmann::json json = 420;
    std::optional<int> val = json;
    ASSERT_TRUE(val.has_value());
    ASSERT_EQ(*val, 420);
    json = nullptr;
    json.get_to(val);
    ASSERT_FALSE(val.has_value());
}

TEST(from_json, vectorOfOptionalInts) {
    nlohmann::json json = { 420, nullptr };
    std::vector<std::optional<int>> vals = json;
    ASSERT_EQ(vals.size(), 2);
    ASSERT_TRUE(vals.at(0).has_value());
    ASSERT_EQ(*vals.at(0), 420);
    ASSERT_FALSE(vals.at(1).has_value());
}

TEST(valueAt, simpleObject) {
    auto simple = R"({ "hello": "world" })"_json;

    ASSERT_EQ(valueAt(simple, "hello"), "world");

    auto nested = R"({ "hello": { "world": "" } })"_json;

    auto & nestedObject = valueAt(nested, "hello");

    ASSERT_EQ(valueAt(nestedObject, "world"), "");
}

TEST(valueAt, missingKey) {
    auto json = R"({ "hello": { "nested": "world" } })"_json;

    ASSERT_THROW(valueAt(json, "foo"), Error);
}

TEST(getObject, rightAssertions) {
    auto simple = R"({ "object": {} })"_json;

    ASSERT_EQ(getObject(valueAt(simple, "object")), R"({})"_json);

    auto nested = R"({ "object": { "object": {} } })"_json;

    auto & nestedObject = getObject(valueAt(nested, "object"));

    ASSERT_EQ(nestedObject, nlohmann::json::parse(R"({ "object": {} })"));
    ASSERT_EQ(getObject(valueAt(nestedObject, "object")), R"({})"_json);
}

TEST(getObject, wrongAssertions) {
    auto json = R"({ "object": {}, "array": [], "string": "", "int": 0, "boolean": false })"_json;

    ASSERT_THROW(getObject(valueAt(json, "array")), Error);
    ASSERT_THROW(getObject(valueAt(json, "string")), Error);
    ASSERT_THROW(getObject(valueAt(json, "int")), Error);
    ASSERT_THROW(getObject(valueAt(json, "boolean")), Error);
}

TEST(getArray, rightAssertions) {
    auto simple = R"({ "array": [] })"_json;

    ASSERT_EQ(getArray(valueAt(simple, "array")), R"([])"_json);
}

TEST(getArray, wrongAssertions) {
    auto json = R"({ "object": {}, "array": [], "string": "", "int": 0, "boolean": false })"_json;

    ASSERT_THROW(getArray(valueAt(json, "object")), Error);
    ASSERT_THROW(getArray(valueAt(json, "string")), Error);
    ASSERT_THROW(getArray(valueAt(json, "int")), Error);
    ASSERT_THROW(getArray(valueAt(json, "boolean")), Error);
}

TEST(getString, rightAssertions) {
    auto simple = R"({ "string": "" })"_json;

    ASSERT_EQ(getString(valueAt(simple, "string")), "");
}

TEST(getString, wrongAssertions) {
    auto json = R"({ "object": {}, "array": [], "string": "", "int": 0, "boolean": false })"_json;

    ASSERT_THROW(getString(valueAt(json, "object")), Error);
    ASSERT_THROW(getString(valueAt(json, "array")), Error);
    ASSERT_THROW(getString(valueAt(json, "int")), Error);
    ASSERT_THROW(getString(valueAt(json, "boolean")), Error);
}

TEST(getInteger, rightAssertions) {
    auto simple = R"({ "int": 0 })"_json;

    ASSERT_EQ(getInteger(valueAt(simple, "int")), 0);
}

TEST(getInteger, wrongAssertions) {
    auto json = R"({ "object": {}, "array": [], "string": "", "int": 0, "boolean": false })"_json;

    ASSERT_THROW(getInteger(valueAt(json, "object")), Error);
    ASSERT_THROW(getInteger(valueAt(json, "array")), Error);
    ASSERT_THROW(getInteger(valueAt(json, "string")), Error);
    ASSERT_THROW(getInteger(valueAt(json, "boolean")), Error);
}

TEST(getBoolean, rightAssertions) {
    auto simple = R"({ "boolean": false })"_json;

    ASSERT_EQ(getBoolean(valueAt(simple, "boolean")), false);
}

TEST(getBoolean, wrongAssertions) {
    auto json = R"({ "object": {}, "array": [], "string": "", "int": 0, "boolean": false })"_json;

    ASSERT_THROW(getBoolean(valueAt(json, "object")), Error);
    ASSERT_THROW(getBoolean(valueAt(json, "array")), Error);
    ASSERT_THROW(getBoolean(valueAt(json, "string")), Error);
    ASSERT_THROW(getBoolean(valueAt(json, "int")), Error);
}

} /* namespace nix */
