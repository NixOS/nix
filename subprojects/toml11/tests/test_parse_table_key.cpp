#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_table_bare_key)
{
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "[barekey]",  std::vector<key>(1, "barekey"));
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "[bare-key]", std::vector<key>(1, "bare-key"));
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "[bare_key]", std::vector<key>(1, "bare_key"));
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "[1234]",     std::vector<key>(1, "1234"));
}

BOOST_AUTO_TEST_CASE(test_table_quoted_key)
{
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "[\"127.0.0.1\"]",          std::vector<key>(1, "127.0.0.1"         ));
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "[\"character encoding\"]", std::vector<key>(1, "character encoding"));
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "[\"ʎǝʞ\"]",                std::vector<key>(1, "ʎǝʞ"               ));
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "['key2']",                 std::vector<key>(1, "key2"              ));
    TOML11_TEST_PARSE_EQUAL(parse_table_key, "['quoted \"value\"']",     std::vector<key>(1, "quoted \"value\""  ));
}

BOOST_AUTO_TEST_CASE(test_table_dotted_key)
{
    {
        std::vector<key> keys(2);
        keys[0] = "physical";
        keys[1] = "color";
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[physical.color]", keys);
    }
    {
        std::vector<key> keys(2);
        keys[0] = "physical";
        keys[1] = "shape";
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[physical.shape]", keys);
    }
    {
        std::vector<key> keys(4);
        keys[0] = "x";
        keys[1] = "y";
        keys[2] = "z";
        keys[3] = "w";
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[x.y.z.w]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[x . y . z . w]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[x. y .z. w]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[x .y. z .w]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[ x. y .z . w ]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[ x . y . z . w ]", keys);
    }
    {
        std::vector<key> keys(2);
        keys[0] = "site";
        keys[1] = "google.com";
        TOML11_TEST_PARSE_EQUAL(parse_table_key, "[site.\"google.com\"]", keys);
    }
}

BOOST_AUTO_TEST_CASE(test_array_of_table_bare_key)
{
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[barekey]]",  std::vector<key>(1, "barekey"));
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[bare-key]]", std::vector<key>(1, "bare-key"));
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[bare_key]]", std::vector<key>(1, "bare_key"));
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[1234]]",     std::vector<key>(1, "1234"));
}

BOOST_AUTO_TEST_CASE(test_array_of_table_quoted_key)
{
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[\"127.0.0.1\"]]",          std::vector<key>(1, "127.0.0.1"         ));
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[\"character encoding\"]]", std::vector<key>(1, "character encoding"));
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[\"ʎǝʞ\"]]",                std::vector<key>(1, "ʎǝʞ"               ));
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[['key2']]",                 std::vector<key>(1, "key2"              ));
    TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[['quoted \"value\"']]",     std::vector<key>(1, "quoted \"value\""  ));
}

BOOST_AUTO_TEST_CASE(test_array_of_table_dotted_key)
{
    {
        std::vector<key> keys(2);
        keys[0] = "physical";
        keys[1] = "color";
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[physical.color]]", keys);
    }
    {
        std::vector<key> keys(2);
        keys[0] = "physical";
        keys[1] = "shape";
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[physical.shape]]", keys);
    }
    {
        std::vector<key> keys(4);
        keys[0] = "x";
        keys[1] = "y";
        keys[2] = "z";
        keys[3] = "w";
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[x.y.z.w]]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[x . y . z . w]]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[x. y .z. w]]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[x .y. z .w]]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[ x. y .z . w ]]", keys);
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[ x . y . z . w ]]", keys);

    }
    {
        std::vector<key> keys(2);
        keys[0] = "site";
        keys[1] = "google.com";
        TOML11_TEST_PARSE_EQUAL(parse_array_table_key, "[[site.\"google.com\"]]", keys);
    }
}
