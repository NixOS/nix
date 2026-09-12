#include <algorithm>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/store/derivation/cbor.hh"
#include "nix/store/derivation/aterm.hh"
#include "derivation/test-support.hh"

namespace nix {
namespace {

using nlohmann::json;
using namespace std::string_literals;
using namespace std::string_view_literals;

const auto golden =
    "\xa8\x63"
    "env\xa2\x41z\x41\x00\x42"
    "aa\x41\xff\x64"
    "args\x81\x41"
    "a\x64"
    "name\x61n\x66inputs\xa2\x64"
    "drvs\xa0\x64srcs\x80\x66system\x61s\x67"
    "builder\x41"
    "b\x67outputs\xa0\x67version\x01"s;

json binary(std::string_view value)
{
    return json::binary(std::vector<uint8_t>(value.begin(), value.end()));
}

json fixture()
{
    return {
        {"version", 1u},
        {"name", "n"},
        {"system", "s"},
        {"builder", binary("b")},
        {"args", {binary("a")}},
        {"env", {{"aa", binary("\xff")}, {"z", binary("\0"sv)}}},
        {"outputs", json::object()},
        {"inputs", {{"srcs", json::array()}, {"drvs", json::object()}}},
    };
}

std::string encodeValue(const json & value)
{
    auto bytes = json::to_cbor(value);
    return {bytes.begin(), bytes.end()};
}

// nlohmann's decoder cannot represent byte-string map keys. Use its encoder
// for individual items and construct the environment map independently.
std::string encodeMap(const std::vector<std::pair<json, std::string>> & entries)
{
    auto bytes = encodeValue(entries.size());
    bytes[0] |= 0xa0;
    for (auto & [key, value] : entries) {
        bytes += encodeValue(key);
        bytes += value;
    }
    return bytes;
}

std::string encode(const json & value)
{
    std::vector<std::pair<json, std::string>> entries;
    for (auto & [key, child] : value.items()) {
        if (key == "env" && child.is_object()) {
            std::vector<std::pair<json, std::string>> env;
            for (auto & [name, content] : child.items())
                env.emplace_back(binary(name), encodeValue(content));
            entries.emplace_back(key, encodeMap(env));
        } else
            entries.emplace_back(key, encodeValue(child));
    }
    return encodeMap(entries);
}

TEST_F(DerivationTest, CborReferenceGolden)
{
    auto drv = derivation::parseCbor(golden, mockXpSettings);
    EXPECT_EQ(drv.name, "n");
    EXPECT_EQ(drv.env.at("aa"), "\xff"s);
    EXPECT_EQ(drv.env.at("z"), "\x00"s);
    EXPECT_EQ(derivation::toCbor(drv), golden);
    EXPECT_EQ(
        derivation::unparse(drv, *store), "Derive([],[],[],\"s\",\"b\",[\"a\"],[(\"aa\",\"\xff\"),(\"z\",\"\x00\")])"s);
}

TEST_F(DerivationTest, CborRejectsTruncationsAndInvalidWireTypes)
{
    for (size_t size = 0; size < golden.size(); ++size)
        EXPECT_THROW(derivation::parseCbor(std::string_view(golden).substr(0, size)), Error) << size;
    for (auto bytes :
         {golden + "\x00"s,
          "\xc0" + golden,
          "\xbf\xff"s,
          "\xa2\x61x\x00\x61x\x00"s,
          "\xbb\xff\xff\xff\xff\xff\xff\xff\xff"s})
        EXPECT_THROW(derivation::parseCbor(bytes), Error);
}

TEST_F(DerivationTest, CborRejectsInvalidSchema)
{
    auto original = fixture();
    for (auto & [key, ignored] : original.items()) {
        auto value = original;
        value.erase(key);
        EXPECT_THROW(derivation::parseCbor(encode(value)), Error) << key;
    }
    for (auto bad : {json(nullptr), json("text"), json::array({1, 2})}) {
        auto value = original;
        value["env"]["z"] = bad;
        EXPECT_THROW(derivation::parseCbor(encode(value)), Error);
    }
    for (auto key : {"builder", "args", "env", "name", "system"}) {
        auto value = original;
        value[key] = std::string_view(key) == "name" || std::string_view(key) == "system" ? binary("x") : json("text");
        EXPECT_THROW(derivation::parseCbor(encode(value)), Error) << key;
    }
    auto value = original;
    value["args"] = {"text"};
    EXPECT_THROW(derivation::parseCbor(encode(value)), Error);
    value = original;
    value["extra"] = 1;
    EXPECT_THROW(derivation::parseCbor(encode(value)), Error);
    value = original;
    value["version"] = 4;
    EXPECT_THROW(derivation::parseCbor(encode(value)), Error);
    value = original;
    value["inputs"]["srcs"] = {"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep", "c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep"};
    EXPECT_THROW(derivation::parseCbor(encode(value)), Error);
    value = original;
    value["name"] = "\xff";
    EXPECT_THROW(derivation::parseCbor(encode(value)), Error);
}

TEST_F(DerivationTest, CborNormalizesEncoding)
{
    auto expected = derivation::parseCbor(golden);
    auto expectedPath = computeStorePath(*store, expected);
    for (auto & bytes : {
             encode(fixture()), // Maps ordered lexically instead of by encoded key length.
             golden.substr(0, golden.size() - 1) + "\x18\x01"s,
             golden.substr(0, golden.size() - 1) + "\x19\x00\x01"s,
             golden.substr(0, golden.size() - 1) + "\x1a\x00\x00\x00\x01"s,
             golden.substr(0, golden.size() - 1) + "\x1b\x00\x00\x00\x00\x00\x00\x00\x01"s,
             "\xb8\x08" + golden.substr(1),
         }) {
        ASSERT_NE(bytes, golden);
        auto drv = derivation::parseCbor(bytes);
        EXPECT_EQ(derivation::toCbor(drv), golden);
        EXPECT_EQ(computeStorePath(*store, drv), expectedPath);
    }
}

TEST_F(DerivationTest, CborNormalizesSetsPreservingIdentity)
{
    auto value = fixture();
    value["inputs"]["srcs"] = {"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-a", "c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-z"};
    auto & outputs = value["inputs"]["drvs"]["c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep.drv"];
    outputs = {{"outputs", {"a", "z"}}, {"dynamicOutputs", json::object()}};
    auto expected = derivation::parseCbor(encode(value));
    auto canonical = derivation::toCbor(expected);
    auto expectedPath = computeStorePath(*store, expected);

    std::reverse(value["inputs"]["srcs"].begin(), value["inputs"]["srcs"].end());
    std::reverse(outputs["outputs"].begin(), outputs["outputs"].end());
    auto bytes = encode(value);
    ASSERT_NE(bytes, canonical);
    auto drv = derivation::parseCbor(bytes);
    EXPECT_EQ(derivation::toCbor(drv), canonical);
    EXPECT_EQ(computeStorePath(*store, drv), expectedPath);
}

TEST_F(DerivationTest, CborEnvironmentKeyTypesAndDuplicates)
{
    auto drv = derivation::parseCbor(golden);
    drv.env.clear();
    auto empty = derivation::toCbor(drv);
    auto position = empty.find(
        "\x63"
        "env\xa0"s);
    ASSERT_NE(position, std::string::npos);
    for (auto env : {
             encodeMap({{"text", encodeValue(binary("value"))}}),
             encodeMap({{binary("\xff"), encodeValue(binary("a"))}, {binary("\xff"), encodeValue(binary("b"))}}),
             encodeMap({{binary("x"), encodeValue("text")}}),
         }) {
        auto malformed = empty;
        malformed.replace(position + 4, 1, env);
        EXPECT_THROW(derivation::parseCbor(malformed), Error);
    }
}

TEST_F(DerivationTest, CborArbitraryBytes)
{
    std::string allBytes;
    for (unsigned i = 0; i < 256; ++i)
        allBytes += static_cast<char>(i);
    auto drv = derivation::parseCbor(golden);
    drv.builder = allBytes;
    drv.args = {"", allBytes, "\xc3\xa9"};
    drv.env = {{"", ""}, {allBytes, allBytes}, {"\x7f", "ascii"}, {"\x80", "non-UTF-8"}};
    auto bytes = derivation::toCbor(drv);
    auto decoded = derivation::parseCbor(bytes);
    EXPECT_EQ(decoded, drv);
    EXPECT_EQ(derivation::unparse(decoded, *store), derivation::unparse(drv, *store));
    EXPECT_EQ(computeStorePath(*store, decoded), computeStorePath(*store, drv));
    EXPECT_EQ(derivation::toCbor(decoded), bytes);

    auto stored = derivation::unparse(decoded, *store);
    EXPECT_EQ(
        derivation::parse(
            *store, std::move(stored), drv.name, derivation::defaultSupportWindowsStoreDir, mockXpSettings),
        drv);

    auto expected = fixture();
    expected["builder"] = binary(allBytes);
    expected["args"] = {binary(""), binary(allBytes), binary("\xc3\xa9")};
    expected["env"] = json::object();
    for (auto & [name, value] : drv.env)
        expected["env"][name] = binary(value);
    EXPECT_EQ(derivation::parseCbor(encode(expected)), drv);

    StorePath path{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-bytes.drv"};
    EXPECT_EQ(
        derivation::toCbor(std::map<StorePath, Derivation>{{path, drv}}),
        encodeMap({
            {"version", encodeValue(1u)},
            {"derivations", encodeMap({{path.to_string(), bytes}})},
        }));

    // This payload illustrates the JSON wire format's remaining limitation.
    EXPECT_THROW(json(drv).dump(), json::type_error);
}

TEST_F(DerivationTest, CborBinaryKeyOrdering)
{
    auto drv = derivation::parseCbor(golden);
    drv.env = {{"\x80", ""}, {"\x7f", ""}, {"aa", ""}};
    auto bytes = derivation::toCbor(drv);
    EXPECT_NE(
        bytes.find(
            "\x63"
            "env\xa3\x41\x7f\x40\x41\x80\x40\x42"
            "aa\x40"s),
        std::string::npos);
}

TEST_F(DerivationTest, CborDerivationCollection)
{
    auto drv = derivation::parseCbor(golden);
    drv.env.clear();
    auto expected = derivation::toCbor(drv);
    StorePath first{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-first.drv"};
    StorePath second{"c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-second.drv"};
    std::map<StorePath, Derivation> drvs{{first, drv}, {second, drv}};
    auto bytes = derivation::toCbor(drvs);
    auto value = json::from_cbor(bytes);
    EXPECT_EQ(value.at("version"), 1);
    EXPECT_EQ(value.at("derivations").size(), 2u);
    for (auto & [path, entry] : value.at("derivations").items())
        EXPECT_EQ(derivation::toCbor(derivation::parseCbor(encode(entry))), expected);
    EXPECT_EQ(derivation::toCbor(drvs), bytes);
}

TEST_F(DynDerivationTest, CborFixtureRoundTrips)
{
    for (auto name : {"simple-derivation", "dyn-dep-derivation"}) {
        auto drv = nlohmann::adl_serializer<Derivation>::from_json(
            json::parse(readFile(goldenMaster(std::string(name) + ".json"))), mockXpSettings);
        auto decoded = derivation::parseCbor(derivation::toCbor(drv), mockXpSettings);
        EXPECT_EQ(decoded, drv);
        EXPECT_EQ(derivation::unparse(decoded, *store), derivation::unparse(drv, *store));
    }
}

TEST_F(DerivationTest, CborStructuredAttrs)
{
    auto drv = derivation::parseCbor(golden);
    drv.env.clear();
    drv.structuredAttrs = StructuredAttrs{json::parse(R"({"z":-0.0,"a":18446744073709551615})")};
    auto bytes = derivation::toCbor(drv);
    auto value = json::from_cbor(bytes);
    EXPECT_TRUE(value.at("structuredAttrs").is_binary());
    auto decoded = derivation::parseCbor(bytes);
    EXPECT_EQ(decoded, drv);
    EXPECT_EQ(derivation::unparse(decoded, *store), derivation::unparse(drv, *store));
    value["structuredAttrs"] = json::binary({'[', ']'});
    EXPECT_THROW(derivation::parseCbor(encode(value)), Error);
}

TEST_F(DerivationTest, CborStringLengthsAndUtf8)
{
    auto drv = derivation::parseCbor(golden);
    for (auto size : {0, 23, 24, 255, 256, 65535, 65536}) {
        drv.env = {{std::string(size, '\xff'), std::string(size, '\xff')}};
        drv.args = {std::string(size, '\xff')};
        drv.builder = std::string(size, 'x');
        auto bytes = derivation::toCbor(drv);
        EXPECT_EQ(derivation::parseCbor(bytes), drv);
        EXPECT_NE(bytes.find(encodeValue(binary(drv.builder))), std::string::npos);
    }
    drv.name = "\xc3\xa9";
    EXPECT_EQ(derivation::parseCbor(derivation::toCbor(drv)), drv);
    drv.name = "\xff";
    EXPECT_THROW(derivation::toCbor(drv), Error);
}

TEST_F(DerivationTest, CborPreservesStructuredAttrsIdentity)
{
    for (const std::string text :
         {R"({ "z": 0, "a": 1 })",
          R"({"escaped":"\u0061","number":1e+02,"negativeZero":-0.0})",
          "{\n  \"a\": 1, \"a\": 2\n}\n"}) {
        auto drv = derivation::parseCbor(golden);
        drv.env.clear();
        drv.structuredAttrs = StructuredAttrs::parse(text);
        auto aterm = derivation::unparse(drv, *store);
        auto imported = derivation::parse(
            *store, std::string(aterm), drv.name, derivation::defaultSupportWindowsStoreDir, mockXpSettings);
        auto bytes = derivation::toCbor(imported);
        auto payload = json::from_cbor(bytes).at("structuredAttrs").get_binary();
        EXPECT_EQ(std::string(payload.begin(), payload.end()), text);
        auto decoded = derivation::parseCbor(bytes);
        EXPECT_EQ(decoded, imported);
        EXPECT_EQ(decoded.structuredAttrs->unparse().second, text);
        EXPECT_EQ(derivation::unparse(decoded, *store), aterm);
        EXPECT_EQ(computeStorePath(*store, decoded), computeStorePath(*store, imported));
        EXPECT_EQ(derivation::toCbor(decoded), bytes);

        auto collection = json::from_cbor(
            derivation::toCbor(std::map<StorePath, Derivation>{{computeStorePath(*store, imported), imported}}));
        EXPECT_EQ(collection.at("derivations").begin()->at("structuredAttrs"), json::binary(payload));
    }
}

TEST_F(DerivationTest, CborOutputVariants)
{
    mockXpSettings.set("experimental-features", "ca-derivations dynamic-derivations impure-derivations");
    for (auto name :
         {"inputAddressed", "caFixedFlat", "caFixedNAR", "caFixedText", "caFloating", "deferred", "impure"}) {
        auto value = fixture();
        value["outputs"]["out"] = json::parse(readFile(goldenMaster("output-"s + name + ".json")));
        auto drv = derivation::parseCbor(encode(value), mockXpSettings);
        EXPECT_EQ(derivation::parseCbor(derivation::toCbor(drv), mockXpSettings), drv);
    }
}

TEST_F(DynDerivationTest, CborDynamicDepthLimit)
{
    auto value = fixture();
    json node = {{"outputs", {"out"}}, {"dynamicOutputs", json::object()}};
    for (unsigned depth = 0; depth < 256; ++depth)
        node = {{"outputs", json::array()}, {"dynamicOutputs", {{"out", std::move(node)}}}};
    auto path = "c015dhfh5l0lp6wxyvdn7bmwhbbr6hr9-dep.drv";
    value["inputs"]["drvs"][path] = node;
    EXPECT_NO_THROW(derivation::parseCbor(encode(value), mockXpSettings));
    value["inputs"]["drvs"][path] = {{"outputs", json::array()}, {"dynamicOutputs", {{"out", node}}}};
    EXPECT_THROW(derivation::parseCbor(encode(value), mockXpSettings), Error);
}

} // namespace
} // namespace nix
