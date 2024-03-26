#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_inline_table)
{
    TOML11_TEST_PARSE_EQUAL_VAT(parse_inline_table<toml::value>, "{}", table());
    {
        table t;
        t["foo"] = toml::value(42);
        t["bar"] = toml::value("baz");
        TOML11_TEST_PARSE_EQUAL_VAT(parse_inline_table<toml::value>, "{foo = 42, bar = \"baz\"}", t);
    }
    {
        table t;
        table t_sub;
        t_sub["name"] = toml::value("pug");
        t["type"] = toml::value(t_sub);
        TOML11_TEST_PARSE_EQUAL_VAT(parse_inline_table<toml::value>, "{type.name = \"pug\"}", t);
    }
}

BOOST_AUTO_TEST_CASE(test_inline_table_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "{}", value(table()));
    {
        table t;
        t["foo"] = toml::value(42);
        t["bar"] = toml::value("baz");
        TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "{foo = 42, bar = \"baz\"}", value(t));
    }
    {
        table t;
        table t_sub;
        t_sub["name"] = toml::value("pug");
        t["type"] = toml::value(t_sub);
        TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "{type.name = \"pug\"}", value(t));
    }
}

BOOST_AUTO_TEST_CASE(test_inline_table_immutability)
{
    {
        std::istringstream stream(std::string(
                "a = {b = 1}\n"
                "a.c = 2\n"));
        BOOST_CHECK_THROW(toml::parse(stream), toml::syntax_error);
    }
    {
        std::istringstream stream(std::string(
                "a = {b = {c = 1}}\n"
                "a.b.d = 2\n"));
        BOOST_CHECK_THROW(toml::parse(stream), toml::syntax_error);
    }
}
