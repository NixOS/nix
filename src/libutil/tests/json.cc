#include "json.hh"
#include <gtest/gtest.h>
#include <sstream>

namespace nix {

    /* ----------------------------------------------------------------------------
     * toJSON
     * --------------------------------------------------------------------------*/

    TEST(toJSON, quotesCharPtr) {
        const char* input = "test";
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "\"test\"");
    }

    TEST(toJSON, quotesStdString) {
        std::string input = "test";
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "\"test\"");
    }

    TEST(toJSON, convertsNullptrtoNull) {
        auto input = nullptr;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "null");
    }

    TEST(toJSON, convertsNullToNull) {
        const char* input = 0;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "null");
    }


    TEST(toJSON, convertsFloat) {
        auto input = 1.024f;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "1.024");
    }

    TEST(toJSON, floatConversionPrecision) {
        auto input = 1.000001f;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "1.000001");
    }

    TEST(toJSON, floatUsesScientific) {
        auto input = 2.3e-10f;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "2.3e-10");
    }

    TEST(toJSON, convertsDouble) {
        const double input = 1.024;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "1.024");
    }

    TEST(toJSON, doubleConversionPrecision) {
        const double input = 1.00000000001;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "1.00000000001");
    }

    TEST(toJSON, doubleUsesScientific) {
        const double input = 2.3e-10;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "2.3e-10");
    }

    TEST(toJSON, convertsBool) {
        auto input = false;
        std::stringstream out;
        toJSON(out, input);

        ASSERT_EQ(out.str(), "false");
    }

    TEST(toJSON, quotesTab) {
        std::stringstream out;
        toJSON(out, "\t");

        ASSERT_EQ(out.str(), "\"\\t\"");
    }

    TEST(toJSON, quotesNewline) {
        std::stringstream out;
        toJSON(out, "\n");

        ASSERT_EQ(out.str(), "\"\\n\"");
    }

    TEST(toJSON, quotesCreturn) {
        std::stringstream out;
        toJSON(out, "\r");

        ASSERT_EQ(out.str(), "\"\\r\"");
    }

    TEST(toJSON, quotesCreturnNewLine) {
        std::stringstream out;
        toJSON(out, "\r\n");

        ASSERT_EQ(out.str(), "\"\\r\\n\"");
    }

    TEST(toJSON, quotesDoublequotes) {
        std::stringstream out;
        toJSON(out, "\"");

        ASSERT_EQ(out.str(), "\"\\\"\"");
    }

    TEST(toJSON, substringEscape) {
        std::stringstream out;
        const char *s = "foo\t";
        toJSON(out, s+3, s + strlen(s));

        ASSERT_EQ(out.str(), "\"\\t\"");
    }

    /* ----------------------------------------------------------------------------
     * JSONObject
     * --------------------------------------------------------------------------*/

    TEST(JSONObject, emptyObject) {
        std::stringstream out;
        {
            JSONObject t(out);
        }
        ASSERT_EQ(out.str(), "{}");
    }

    TEST(JSONObject, objectWithList) {
        std::stringstream out;
        {
            JSONObject t(out);
            auto l = t.list("list");
            l.elem("element");
        }
        ASSERT_EQ(out.str(), R"#({"list":["element"]})#");
    }

    TEST(JSONObject, objectWithListIndent) {
        std::stringstream out;
        {
            JSONObject t(out, true);
            auto l = t.list("list");
            l.elem("element");
        }
        ASSERT_EQ(out.str(),
R"#({
  "list": [
    "element"
  ]
})#");
    }

    TEST(JSONObject, objectWithPlaceholderAndList) {
        std::stringstream out;
        {
            JSONObject t(out);
            auto l = t.placeholder("list");
            l.list().elem("element");
        }

        ASSERT_EQ(out.str(), R"#({"list":["element"]})#");
    }

    TEST(JSONObject, objectWithPlaceholderAndObject) {
        std::stringstream out;
        {
            JSONObject t(out);
            auto l = t.placeholder("object");
            l.object().attr("key", "value");
        }

        ASSERT_EQ(out.str(), R"#({"object":{"key":"value"}})#");
    }

    /* ----------------------------------------------------------------------------
     * JSONList
     * --------------------------------------------------------------------------*/

    TEST(JSONList, empty) {
        std::stringstream out;
        {
            JSONList l(out);
        }
        ASSERT_EQ(out.str(), R"#([])#");
    }

    TEST(JSONList, withElements) {
        std::stringstream out;
        {
            JSONList l(out);
            l.elem("one");
            l.object();
            l.placeholder().write("three");
        }
        ASSERT_EQ(out.str(), R"#(["one",{},"three"])#");
    }
}

