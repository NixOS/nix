#include "attrs.hh"
#include "error.hh"

#include <nlohmann/json.hpp>
#include <gtest/gtest.h>

using json = nlohmann::json;

namespace nix::fetchers {

    /* ----------------------------------------------------------------------------
     *jsonToAttrs 
     * --------------------------------------------------------------------------*/

    TEST(jsonToAttrs, simpleJson) {
        json j = {
            {"num", 42 },
            {"string", "this is a string" },
            {"bool", true }
        };

        Attrs attrs = jsonToAttrs(j);
        auto it = attrs.find("num");
        ASSERT_NE(it, attrs.end());
        ASSERT_EQ(std::get<int64_t>(it->second), 42);

        it = attrs.find("string");
        ASSERT_NE(it, attrs.end());
        ASSERT_EQ(std::get<std::string>(it->second), "this is a string");

        it = attrs.find("bool");
        ASSERT_NE(it, attrs.end());
        //ASSERT_EQ(std::get<Explicit<bool>>(it->second).t, true); // unexpected index exception
        //ASSERT_EQ(std::get<bool>(it->second).t, true); // static assertion failed: T should occur for exactly once in alternatives  
    }

    TEST(jsonToAttrs, emptyJson) {
        json emptyJson = { };
        Attrs emptyAttrs = { };

        auto res = jsonToAttrs(emptyJson);
        ASSERT_EQ(res.size(), 0);
    }

    /* ----------------------------------------------------------------------------
     * attrsToJson
     * --------------------------------------------------------------------------*/

    TEST(attrsToJon, simpleAttr) {

        Explicit<bool> b = { true };
        json j = {
            {"num", 42 },
            {"string", "this is a string" },
            {"bool", true }
        };

        Attrs attrs = {
          { "num", 42 },
          { "string", "this is a string" },
          { "bool", b }
        };

        ASSERT_EQ(attrsToJson(attrs), j);
    }

    TEST(jsonToAttrs, emptyAttrs) {
        json emptyJson = { };
        Attrs emptyAttrs = { };

        auto res = attrsToJson(emptyAttrs);
        ASSERT_EQ(res.size(), 0);
    }

    /* ----------------------------------------------------------------------------
     * maybeGetStrAttr
     * --------------------------------------------------------------------------*/

    TEST(maybeGetStrAttr, getsNothingFromEmptyAttr) {
        Attrs emptyAttrs = { };

        auto res = maybeGetStrAttr(emptyAttrs, "string");
        ASSERT_FALSE(res.has_value());
    }

    TEST(maybeGetStrAttr, getsStringFromAttr) {
        Attrs emptyAttrs = {
          { "string", "this-is-a-string" }
        };

        auto res = maybeGetStrAttr(emptyAttrs, "string");
        ASSERT_TRUE(res.has_value());
        ASSERT_EQ(res.value(), "this-is-a-string");
    }

    TEST(maybeGetStrAttr, throwsWhenValueIsNotAString) {
        Attrs emptyAttrs = {
          { "string", 42 }
        };

        ASSERT_THROW(maybeGetStrAttr(emptyAttrs, "string"), Error);
    }

    /* ----------------------------------------------------------------------------
     * getStrAttr
     * --------------------------------------------------------------------------*/

    TEST(getStrAttr, throwsOnEmptyAttr) {
        Attrs emptyAttrs = { };

        ASSERT_THROW(getStrAttr(emptyAttrs, "string"), Error);
    }

    TEST(getStrAttr, getsStringFromAttr) {
        Attrs emptyAttrs = {
          { "string", "this-is-a-string" }
        };

        ASSERT_EQ(getStrAttr(emptyAttrs, "string"), "this-is-a-string");
    }

    TEST(getStrAttr, throwsWhenValueIsNotAString) {
        Attrs emptyAttrs = {
          { "string", 42 }
        };

        ASSERT_THROW(getStrAttr(emptyAttrs, "string"), Error);
    }

    /* ----------------------------------------------------------------------------
     * maybeGetIntAttr
     * --------------------------------------------------------------------------*/

    TEST(maybeGetIntAttr, throwsOnEmptyAttr) {
        Attrs emptyAttrs = { };

        auto res = maybeGetIntAttr(emptyAttrs, "string");
        ASSERT_FALSE(res.has_value());

    }

    TEST(maybeGetIntAttr, getsIntFromAttr) {
        Attrs emptyAttrs = {
          { "int", 42 }
        };

        auto res = maybeGetIntAttr(emptyAttrs, "int");
        ASSERT_TRUE(res.has_value());
        ASSERT_EQ(res.value(), 42);
    }

    TEST(maybeGetIntAttr, throwsWhenValueIsNotAString) {
        Attrs emptyAttrs = {
          { "int", "42" }
        };

        ASSERT_THROW(maybeGetIntAttr(emptyAttrs, "int"), Error);
    }

    /* ----------------------------------------------------------------------------
     * getIntAttr
     * --------------------------------------------------------------------------*/

    TEST(getIntAttr, throwsOnEmptyAttr) {
        Attrs emptyAttrs = { };

        ASSERT_THROW(getIntAttr(emptyAttrs, "int"), Error);
    }

    TEST(getIntAttr, getsIntFromAttr) {
        Attrs emptyAttrs = {
          { "int", 42 }
        };

        ASSERT_EQ(getIntAttr(emptyAttrs, "int"), 42);
    }

    TEST(getIntAttr, throwsWhenValueIsNotAnInt) {
        Attrs emptyAttrs = {
          { "int", "42" }
        };

        ASSERT_THROW(getIntAttr(emptyAttrs, "int"), Error);
    }

    /* ----------------------------------------------------------------------------
     * maybeGetBoolAttr
     * --------------------------------------------------------------------------*/

    TEST(maybeGetBoolAttr, throwsOnEmptyAttr) {
        Attrs emptyAttrs = { };

        auto res = maybeGetBoolAttr(emptyAttrs, "bool");
        ASSERT_FALSE(res.has_value());

    }

    TEST(maybeGetBoolAttr, getsBoolFromAttr) {
        Attrs emptyAttrs = {
          { "bool", true }
        };

        auto res = maybeGetBoolAttr(emptyAttrs, "bool");
        ASSERT_TRUE(res.has_value());
        ASSERT_EQ(res.value(), true);
    }

    TEST(maybeGetBoolAttr, throwsWhenValueIsNotABool) {
        Attrs emptyAttrs = {
          { "bool", "42" }
        };

        ASSERT_THROW(maybeGetBoolAttr(emptyAttrs, "bool"), Error);
    }

    /* ----------------------------------------------------------------------------
     * getBoolAttr
     * --------------------------------------------------------------------------*/

    TEST(getBoolAttr, throwsOnEmptyAttr) {
        Attrs emptyAttrs = { };

        ASSERT_THROW(getBoolAttr(emptyAttrs, "bool"), Error);
    }

    TEST(getBoolAttr, getsBoolFromAttr) {
        Attrs emptyAttrs = {
          { "bool", true }
        };

        ASSERT_EQ(getBoolAttr(emptyAttrs, "bool"), true);
    }

    TEST(getBoolAttr, throwsWhenValueIsNotABool) {
        Attrs emptyAttrs = {
          { "bool", "42" }
        };

        ASSERT_THROW(getBoolAttr(emptyAttrs, "bool"), Error);
    }

    TEST(getBoolAttr, getsNonZeroIntsCastToTrue) {
        Attrs emptyAttrs = {
          { "bool", 42 }
        };

        ASSERT_EQ(getBoolAttr(emptyAttrs, "bool"), true);
    }

    TEST(getBoolAttr, getsZeroIntsCastToFalse) {
        Attrs emptyAttrs = {
          { "bool", 0 }
        };

        ASSERT_EQ(getBoolAttr(emptyAttrs, "bool"), false);
    }

    /* ----------------------------------------------------------------------------
     * attrsToQuery
     * --------------------------------------------------------------------------*/

    TEST(attrsToQuery, returnsEmptyMapOnEmptyAttrs) {
        Attrs emptyAttrs = { };
        std::map<std::string, std::string> emptyMap = { };

        ASSERT_EQ(attrsToQuery(emptyAttrs), emptyMap);
    }

    TEST(attrsToQuery, stringifiesAttr) {

        Explicit<bool> b = { true };
        Attrs attrs = {
          { "num", 42 },
          { "string", "this is a string" },
          { "bool", b }
        };

        std::map<std::string, std::string> expected = {
            { "num", "42" },
            { "string", "this is a string" },
            { "bool", "1" }
        };

        ASSERT_EQ(attrsToQuery(attrs), expected);
    }
}
