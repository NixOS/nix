#ifdef HAVE_RYML

#  include <cstring>
#  include "tests/libexpr.hh"
#  include "primops.hh"

// access to the json sax parser is required
#  include "json-to-value-sax.hh"

namespace {
using namespace nix;
using FromYAMLFun = Value(EvalState &, Value, std::optional<Value>);

/**
 * replacement of non-ascii unicode characters, which indicate the presence of certain characters that would be
 * otherwise hard to read
 */
std::string replaceUnicodePlaceholders(std::string_view str)
{
    constexpr std::string_view eop("\xe2\x88\x8e");
    constexpr std::string_view filler{"\xe2\x80\x94"};
    constexpr std::string_view space{"\xe2\x90\xa3"};
    constexpr std::string_view newLine{"\xe2\x86\xb5"};
    constexpr std::string_view tab("\xc2\xbb");
    auto data = str.begin();
    std::string::size_type last = 0;
    const std::string::size_type size = str.size();
    std::string ret;
    ret.reserve(size);
    for (std::string::size_type i = 0; i < size; i++) {
        if ((str[i] & 0xc0) == 0xc0) {
            char replaceWith = '\0';
            std::string::size_type seqSize = 1;
            std::string::size_type remSize = size - i;
            if (remSize >= 3 && (filler.find(data + i, 0, 3) != eop.find(data + i, 0, 3))) {
                seqSize = 3;
            } else if (remSize >= 3 && space.find(data + i, 0, 3) != space.npos) {
                replaceWith = ' ';
                seqSize = 3;
            } else if (remSize >= 3 && newLine.find(data + i, 0, 3) != newLine.npos) {
                seqSize = 3;
            } else if (remSize >= 2 && tab.find(data + i, 0, 2) != tab.npos) {
                replaceWith = '\t';
                seqSize = 2;
            } else {
                continue;
            }
            ret.append(str, last, i - last);
            if (replaceWith != '\0') {
                ret.append(&replaceWith, 1);
            }
            last = i + seqSize;
            i += seqSize - 1;
        }
    }
    ret.append(str.begin() + last, str.size() - last);
    return ret;
}

bool parseJSON(EvalState & state, std::istream & s_, Value & v)
{
    auto parser = makeJSONSaxParser(state, v);
    return nlohmann::json::sax_parse(s_, parser.get(), nlohmann::json::input_format_t::json, false);
}

Value parseJSONStream(EvalState & state, std::string_view json, std::function<FromYAMLFun> fromYAML)
{
    std::stringstream ss;
    ss << json;
    std::list<Value> list;
    Value root, refJson;
    std::streampos start = 0;
    try {
        while (ss.peek() != EOF && json.size() - ss.tellg() > 1) {
            parseJSON(state, ss, refJson);
            list.emplace_back(refJson);
            // sanity check: builtins.fromJSON and builtins.fromYAML should return the same result when applied to a
            // JSON string
            root.mkString(std::string_view(json.begin() + start, ss.tellg() - start));
            Value rymlJson = fromYAML(state, root, {});
            EXPECT_EQ(printValue(state, refJson), printValue(state, rymlJson));
            start = ss.tellg() + std::streampos(1);
        }
    } catch (const std::exception & e) {
    }
    if (list.size() == 1) {
        root = *list.begin();
    } else {
        ListBuilder list_builder(state, list.size());
        size_t i = 0;
        for (auto val : list) {
            *(list_builder[i++] = state.allocValue()) = val;
        }
        root.mkList(list_builder);
    }
    return root;
}

} /* namespace */

namespace nix {
// Testing the conversion from YAML

class FromYAMLTest : public LibExprTest
{
protected:
    static std::function<FromYAMLFun> getFromYAML()
    {
        static std::function<FromYAMLFun> fromYAML = []() {
            for (const auto & primOp : *RegisterPrimOp::primOps) {
                if (primOp.name == "__fromYAML") {
                    auto primOpFun = primOp.fun;
                    std::function<FromYAMLFun> function =
                        [=](EvalState & state, Value yaml, std::optional<Value> options) {
                            Value emptyOptions, result;
                            auto bindings = state.buildBindings(0);
                            emptyOptions.mkAttrs(bindings);
                            Value * args[3] = {&yaml, options ? &*options : &emptyOptions, nullptr};
                            primOpFun(state, noPos, args, result);
                            return result;
                        };
                    return function;
                }
            }
            ADD_FAILURE() << "The experimental feature \"fromYAML\" is not available";
            return std::function<FromYAMLFun>();
        }();
        return fromYAML;
    }

    Value parseYAML(const char * str, std::optional<Value> options = {})
    {
        Value test;
        test.mkString(str);
        return getFromYAML()(state, test, options);
    }

    void execYAMLTest(std::string_view test)
    {
        auto fromYAML = getFromYAML();
        Value testVal;
        testVal.mkString(test);
        Value testCases = fromYAML(state, testVal, {});
        size_t ctr = 0;
        std::string_view testName;
        Value * json = nullptr;
        for (auto testCase : testCases.listItems()) {
            bool fail = false;
            std::string_view yamlRaw;
            for (auto attr = testCase->attrs()->begin(); attr != testCase->attrs()->end(); attr++) {
                auto name = state.symbols[attr->name];
                if (name == "json") {
                    json = attr->value;
                } else if (name == "yaml") {
                    yamlRaw = attr->value->string_view();
                } else if (name == "fail") {
                    fail = attr->value->boolean();
                } else if (name == "name") {
                    testName = attr->value->string_view();
                }
            }
            // extract expected result
            Value jsonVal;
            bool noJSON = !json || json->type() != nString;
            if (!noJSON) {
                std::string_view jsonStr = json->string_view();
                // Test cases with "json: ''" are parsed as empty JSON and test cases with the value of the "json" node
                // being a block scalar, have no JSON representation, if the block scalar contains the line "null"
                // (indentation 0)
                noJSON = jsonStr.empty()
                         || (jsonStr != "null" && (jsonStr.starts_with("null") || jsonStr.ends_with("null")))
                         || jsonStr.find("\nnull\n") != std::string_view::npos;
                if (!noJSON) {
                    jsonVal = parseJSONStream(state, jsonStr, fromYAML);
                }
            }
            // extract the YAML to be parsed
            std::string yamlStr = replaceUnicodePlaceholders(yamlRaw);
            Value yaml, yamlVal;
            yaml.mkString(yamlStr);
            if (noJSON) {
                EXPECT_THROW(yamlVal = fromYAML(state, yaml, {}), EvalError)
                    << "Testcase #" << ctr
                    << ": YAML has no JSON representation because of empty document or null key, parsed \""
                    << printValue(state, yamlVal) << "\":\n"
                    << yamlRaw;
            } else if (!fail) {
                yamlVal = fromYAML(state, yaml, {});
                EXPECT_EQ(printValue(state, yamlVal), printValue(state, jsonVal))
                    << "Testcase #" << ctr << ": Parsed YAML does not match expected JSON result:\n"
                    << yamlRaw;
            } else {
                EXPECT_THROW(yamlVal = fromYAML(state, yaml, {}), EvalError)
                    << "Testcase #" << ctr << " (" << testName << "): Parsing YAML has to throw an exception, but \""
                    << printValue(state, yamlVal) << "\" was parsed:\n"
                    << yamlRaw;
            }
            ctr++;
        }
    }
};

TEST_F(FromYAMLTest, NoContent)
{
    EXPECT_THROW(parseYAML(""), EvalError);
}

TEST_F(FromYAMLTest, Null)
{
    Value val = parseYAML("[ null, Null, NULL, ~, ]");
    for (auto item : val.listItems()) {
        EXPECT_EQ(item->type(), nNull);
    }
}

TEST_F(FromYAMLTest, NaN)
{
    const char * nans[] = {".nan", ".NaN", ".NAN"};
    for (auto str : nans) {
        Value val = parseYAML(str);
        ASSERT_EQ(val.type(), nFloat);
        NixFloat _float = val.fpoint();
        EXPECT_NE(_float, _float) << "'" << str << "' shall be parsed as NaN";
    }
    const char * strings[] = {"nan", "+nan", "-nan", "+.nan", "-.nan"};
    for (auto str : strings) {
        Value val = parseYAML(str);
        ASSERT_EQ(val.type(), nString) << "'" << str << "' shall not be converted to a floating point type";
        EXPECT_EQ(val.string_view(), std::string_view(str));
    }
}

TEST_F(FromYAMLTest, Inf)
{
    NixFloat inf = std::numeric_limits<NixFloat>::infinity();
    Value val = parseYAML("[ .INF, .Inf, .inf, +.INF, +.Inf, +.inf ]");
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nFloat);
        EXPECT_EQ(item->fpoint(), inf);
    }
    val = parseYAML("[ -.INF, -.Inf, -.inf ]");
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nFloat);
        EXPECT_EQ(item->fpoint(), -inf);
    }
    val = parseYAML("inf");
    ASSERT_EQ(val.type(), nString) << "'inf' shall not be converted to a floating point type";
    EXPECT_EQ(val.string_view(), "inf");
}

TEST_F(FromYAMLTest, Int)
{
    Value val = parseYAML("[ 1, +1, 0x1, 0o1 ]");
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nInt);
        EXPECT_EQ(item->integer(), NixInt(1));
    }

    const char * strings[] = {"+", "0b1", "0B1", "0O1", "0X1", "+0b1", "-0b1", "+0o1", "-0o1", "+0x1", "-0x1"};
    for (auto str : strings) {
        Value val = parseYAML(str);
        ASSERT_EQ(val.type(), nString) << "'" << str << "' shall not be converted to an integer";
        EXPECT_EQ(val.string_view(), str);
    }
}

TEST_F(FromYAMLTest, Float)
{
    Value val = parseYAML("[ !!float 1, !!float 0x1, !!float 0o1, 1., +1., .1e1, +.1e1, 1.0, 10e-1, 10.e-1 ]");
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nFloat);
        EXPECT_EQ(item->fpoint(), 1.);
    }
    val = parseYAML("!!float -0");
    ASSERT_EQ(val.type(), nFloat);
    EXPECT_EQ(1. / val.fpoint(), 1. / -0.) << "\"!!float -0\" shall be parsed as -0.0";

    const char * strings[] = {"0x1.", "0X1.", "0b1.", "0B1.", "0o1.", "0O1"};
    for (auto str : strings) {
        Value val = parseYAML(str);
        ASSERT_EQ(val.type(), nString) << "'" << str << "' shall not be converted to a float";
        EXPECT_EQ(val.string_view(), str);
    }
}

TEST_F(FromYAMLTest, TrueYAML1_2)
{
    Value val = parseYAML("[ true, True, TRUE ]");
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nBool);
        EXPECT_TRUE(item->boolean());
    }
    const char * strings[] = {"y", "Y", "on", "On", "ON", "yes", "Yes", "YES"};
    for (auto str : strings) {
        Value val = parseYAML(str);
        ASSERT_EQ(val.type(), nString) << "'" << str << "' shall not be converted to a boolean";
        EXPECT_EQ(val.string_view(), std::string_view(str));
    }
}

TEST_F(FromYAMLTest, TrueYAML1_1)
{
    Value options;
    auto bindings = state.buildBindings(1);
    bindings.alloc("useBoolYAML1_1").mkBool(true);
    options.mkAttrs(bindings);

    Value val = parseYAML("[ true, True, TRUE, y, Y, on, On, ON, yes, Yes, YES ]", options);
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nBool);
        EXPECT_TRUE(item->boolean());
    }
}

TEST_F(FromYAMLTest, FalseYAML1_2)
{
    Value val = parseYAML("[ false, False, FALSE ]");
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nBool);
        EXPECT_FALSE(item->boolean());
    }
    const char * strings[] = {"n", "N", "no", "No", "NO", "off", "Off", "OFF"};
    for (auto str : strings) {
        Value val = parseYAML(str);
        ASSERT_EQ(val.type(), nString) << "'" << str << "' shall not be converted to a boolean";
        EXPECT_EQ(val.string_view(), std::string_view(str));
    }
}

TEST_F(FromYAMLTest, FalseYAML1_1)
{
    Value options;
    auto bindings = state.buildBindings(1);
    bindings.alloc("useBoolYAML1_1").mkBool(true);
    options.mkAttrs(bindings);

    Value val = parseYAML("[ false, False, FALSE, n, N, no, No, NO, off, Off, OFF ]", options);
    for (auto item : val.listItems()) {
        ASSERT_EQ(item->type(), nBool);
        EXPECT_FALSE(item->boolean());
    }
}

TEST_F(FromYAMLTest, QuotedString)
{
    const char * strings[] = {
        "\"null\"",
        "\"~\"",
        "\"\"",
        "\".inf\"",
        "\"+.inf\"",
        "\"-.inf\"",
        "\".nan\"",
        "\"true\"",
        "\"false\"",
        "\"1\"",
        "\"+1\"",
        "\"-1\"",
        "\"1.0\""};
    for (auto str : strings) {
        Value val = parseYAML(str);
        ASSERT_EQ(val.type(), nString) << "'" << str << "' shall be parsed as string";
        EXPECT_EQ(val.string_view(), std::string_view(&str[1], strlen(str) - 2));
    }
}

TEST_F(FromYAMLTest, Map)
{
    EXPECT_THROW(parseYAML("{ \"2\": 2, 2: null }"), EvalError) << "non-unique keys";
}

} /* namespace nix */

// include auto-generated header
#  include "./yaml-test-suite.hh"

#endif
