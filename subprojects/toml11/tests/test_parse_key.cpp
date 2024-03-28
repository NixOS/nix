#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_bare_key)
{
    TOML11_TEST_PARSE_EQUAL(parse_key, "barekey",  std::vector<key>(1, "barekey"));
    TOML11_TEST_PARSE_EQUAL(parse_key, "bare-key", std::vector<key>(1, "bare-key"));
    TOML11_TEST_PARSE_EQUAL(parse_key, "bare_key", std::vector<key>(1, "bare_key"));
    TOML11_TEST_PARSE_EQUAL(parse_key, "1234",     std::vector<key>(1, "1234"));
}

BOOST_AUTO_TEST_CASE(test_quoted_key)
{
    TOML11_TEST_PARSE_EQUAL(parse_key, "\"127.0.0.1\"",          std::vector<key>(1, "127.0.0.1"         ));
    TOML11_TEST_PARSE_EQUAL(parse_key, "\"character encoding\"", std::vector<key>(1, "character encoding"));
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
    TOML11_TEST_PARSE_EQUAL(parse_key, "\"\xCA\x8E\xC7\x9D\xCA\x9E\"", std::vector<key>(1, "\xCA\x8E\xC7\x9D\xCA\x9E"));
#else
    TOML11_TEST_PARSE_EQUAL(parse_key, "\"ʎǝʞ\"",                std::vector<key>(1, "ʎǝʞ"               ));
#endif
    TOML11_TEST_PARSE_EQUAL(parse_key, "'key2'",                 std::vector<key>(1, "key2"              ));
    TOML11_TEST_PARSE_EQUAL(parse_key, "'quoted \"value\"'",     std::vector<key>(1, "quoted \"value\""  ));
}

BOOST_AUTO_TEST_CASE(test_dotted_key)
{
    {
        std::vector<key> keys(2);
        keys[0] = "physical";
        keys[1] = "color";
        TOML11_TEST_PARSE_EQUAL(parse_key, "physical.color", keys);
    }
    {
        std::vector<key> keys(2);
        keys[0] = "physical";
        keys[1] = "shape";
        TOML11_TEST_PARSE_EQUAL(parse_key, "physical.shape", keys);
    }
    {
        std::vector<key> keys(4);
        keys[0] = "x";
        keys[1] = "y";
        keys[2] = "z";
        keys[3] = "w";
        TOML11_TEST_PARSE_EQUAL(parse_key, "x.y.z.w", keys);
    }
    {
        std::vector<key> keys(2);
        keys[0] = "site";
        keys[1] = "google.com";
        TOML11_TEST_PARSE_EQUAL(parse_key, "site.\"google.com\"", keys);
    }
}
