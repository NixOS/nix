#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_boolean)
{
    TOML11_TEST_PARSE_EQUAL(parse_boolean,  "true",  true);
    TOML11_TEST_PARSE_EQUAL(parse_boolean, "false", false);
}

BOOST_AUTO_TEST_CASE(test_boolean_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,  "true", toml::value( true));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "false", toml::value(false));
}
