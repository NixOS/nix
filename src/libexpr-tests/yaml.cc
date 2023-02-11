#ifdef HAVE_RYML

#include "libexpr.hh"

// Ugly, however direct access to the SAX parser is required in order to parse multiple JSON objects from a stream
#include "json-to-value.cc"


namespace nix {
// Testing the conversion from YAML

    static std::string replaceUnicodePlaceholders(std::string_view str) {
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

    static bool parseJSON(EvalState & state, std::istream & s_, Value & v)
    {
        JSONSax parser(state, v);
        return nlohmann::json::sax_parse(s_, &parser, nlohmann::json::input_format_t::json, false);
    }

    static Value parseJSONStream(EvalState & state, std::string_view json, PrimOpFun fromYAML) {
        std::stringstream ss;
        ss << json;
        std::list<Value> list;
        Value root, refJson;
        Value *pRoot = &root, rymlJson;
        std::streampos start = 0;
        try {
            while (ss.peek() != EOF && json.size() - ss.tellg() > 1) {
                parseJSON(state, ss, refJson);
                list.emplace_back(refJson);
                // sanity check: builtins.fromJSON and builtins.fromYAML should return the same result when applied to a JSON string
                root.mkString(std::string_view(json.begin() + start, ss.tellg() - start));
                fromYAML(state, noPos, &pRoot, rymlJson);
                EXPECT_EQ(printValue(state, refJson), printValue(state, rymlJson));
                start = ss.tellg() + std::streampos(1);
            }
        } catch (const std::exception &e) {
        }
        if (list.size() == 1) {
            root = *list.begin();
        } else {
            state.mkList(root, list.size());
            Value **elems = root.listElems();
            size_t i = 0;
            for (auto val : list) {
                *(elems[i++] = state.allocValue()) = val;
            }
        }
        return root;
    }

    class FromYAMLTest : public LibExprTest {
        protected:

            std::string execYAMLTest(std::string_view test) {
                const PrimOpFun fromYAML = state.getBuiltin("fromYAML").primOp->fun;
                Value testCases, testVal;
                Value *pTestVal = &testVal;
                testVal.mkString(test);
                fromYAML(state, noPos, &pTestVal, testCases);
                int ctr = -1;
                for (auto testCase : testCases.listItems()) {
                    Value *json = nullptr;
                    bool fail = false;
                    ctr++;
                    std::string_view yamlRaw;
                    for (auto attr = testCase->attrs->begin(); attr != testCase->attrs->end(); attr++) {
                        auto name = state.symbols[attr->name];
                        if (name == "json") {
                            json = attr->value;
                        } else if (name == "yaml") {
                            yamlRaw = state.forceStringNoCtx(*attr->value, noPos, "while interpreting the \"yaml\" field as string");
                        } else if (name == "fail") {
                            fail = state.forceBool(*attr->value, noPos, "while interpreting the \"fail\" field as bool");
                        }
                    }
                    fail |= !json;
                    bool emptyJSON = false;
                    std::string_view jsonStr;
                    Value jsonVal;
                    std::string yamlStr = replaceUnicodePlaceholders(yamlRaw);
                    Value yaml, yamlVal;
                    Value *pYaml = &yaml;
                    yaml.mkString(yamlStr);
                    if (!fail) {
                        if (json->type() == nNull) {
                            jsonStr = "null";
                            jsonVal.mkNull();
                        } else {
                            jsonStr = state.forceStringNoCtx(*json, noPos, "while interpreting the \"json\" field as string");
                        }
                        if (!(emptyJSON = jsonStr.empty())) {
                            if (json->type() != nNull) {
                                jsonVal = parseJSONStream(state, jsonStr, fromYAML);
                                jsonStr = printValue(state, jsonVal);
                            }
                            fromYAML(state, noPos, &pYaml, yamlVal);
                            EXPECT_EQ(printValue(state, yamlVal), printValue(state, jsonVal)) << "Testcase[" + std::to_string(ctr) + "]: Parsed YAML does not match expected JSON result";
                        }
                    }
                    if (fail || emptyJSON) {
                        try {
                            fromYAML(state, noPos, &pYaml, yamlVal); // should throw exception, if fail is asserted, and has to throw, if emptyJSON is asserted
                        } catch (const EvalError &e) {
                            continue;
                        }
                    }
                    EXPECT_FALSE(emptyJSON) << "Testcase[" + std::to_string(ctr) + "]: Parsing empty YAML has to fail";
                    // ryml parses some invalid YAML successfully
                    // EXPECT_FALSE(fail) << "Testcase[" << ctr << "]: Invalid YAML was parsed successfully";
                }
                return "OK";
            }
    };

} /* namespace nix */


// include auto-generated header
#include "./yaml-test-suite.hh"

#endif
