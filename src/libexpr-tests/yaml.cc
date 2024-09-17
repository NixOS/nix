#ifdef HAVE_RYML

#include "libexpr.hh"
#include "primops.hh"

// Ugly, however direct access to the SAX parser is required in order to parse multiple JSON objects from a stream
#include "json-to-value.cc"


namespace nix {
// Testing the conversion from YAML

    /* replacement of non-ascii unicode characters, which indicate the presence of certain characters that would be otherwise hard to read */
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

            void execYAMLTest(std::string_view test) {
                //const PrimOpFun fromYAML = state.getBuiltin("fromYAML").primOp->fun;
                PrimOpFun fromYAML = nullptr;
                for (const auto & primOp : *RegisterPrimOp::primOps) {
                    if (primOp.name == "__fromYAML") {
                        fromYAML = primOp.fun;
                    }
                }
                EXPECT_FALSE(fromYAML == nullptr) << "The experimental feature \"fromYAML\" is not available";
                Value testCases, testVal;
                Value *pTestVal = &testVal;
                testVal.mkString(test);
                fromYAML(state, noPos, &pTestVal, testCases);
                size_t ctr = 0;
                std::string_view testName;
                Value *json = nullptr;
                for (auto testCase : testCases.listItems()) {
                    bool fail = false;
                    std::string_view yamlRaw;
                    for (auto attr = testCase->attrs->begin(); attr != testCase->attrs->end(); attr++) {
                        auto name = state.symbols[attr->name];
                        if (name == "json") {
                            json = attr->value;
                        } else if (name == "yaml") {
                            yamlRaw = state.forceStringNoCtx(*attr->value, noPos, "while interpreting the \"yaml\" field as string");
                        } else if (name == "fail") {
                            fail = state.forceBool(*attr->value, noPos, "while interpreting the \"fail\" field as bool");
                        } else if (name == "name") {
                            testName = state.forceStringNoCtx(*attr->value, noPos, "while interpreting the \"name\" field as string");
                        }
                    }
                    // extract expected result
                    Value jsonVal;
                    bool nullJSON = json && json->type() == nNull;
                    bool emptyJSON = !nullJSON;
                    if (json && !nullJSON) {
                        std::string_view jsonStr = state.forceStringNoCtx(*json, noPos, "while interpreting the \"json\" field as string");
                        emptyJSON = jsonStr.empty();
                        if (!emptyJSON) {
                            jsonVal = parseJSONStream(state, jsonStr, fromYAML);
                            jsonStr = printValue(state, jsonVal);
                        }
                    }
                    // extract the YAML to be parsed
                    std::string yamlStr = replaceUnicodePlaceholders(yamlRaw);
                    Value yaml, yamlVal;
                    Value *pYaml = &yaml;
                    yaml.mkString(yamlStr);
                    if (!fail) {
                        if (emptyJSON) {
                            EXPECT_THROW(
                                fromYAML(state, noPos, &pYaml, yamlVal),
                                EvalError) << "Testcase #" << ctr << ": Expected empty YAML, which should throw an exception, parsed \"" << printValue(state, yamlVal) << "\":\n" << yamlRaw;
                        } else {
                            fromYAML(state, noPos, &pYaml, yamlVal);
                            if (nullJSON) {
                                EXPECT_TRUE(yamlVal.type() == nNull) << "Testcase #" << ctr << ": Expected null YAML:\n" << yamlStr;
                            } else {
                                EXPECT_EQ(printValue(state, yamlVal), printValue(state, jsonVal)) << "Testcase #" << ctr << ": Parsed YAML does not match expected JSON result:\n" << yamlRaw;
                            }
                        }
                    } else {
                        EXPECT_THROW(
                            fromYAML(state, noPos, &pYaml, yamlVal),
                            EvalError) << "Testcase #" << ctr << " (" << testName << "): Parsing YAML has to throw an exception, but \"" << printValue(state, yamlVal) << "\" was parsed:\n" << yamlRaw;
                    }
                    ctr++;
                }
            }
    };

} /* namespace nix */


// include auto-generated header
#include "./yaml-test-suite.hh"

#endif
