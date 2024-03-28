#include <toml.hpp>

#include "unit_test.hpp"

BOOST_AUTO_TEST_CASE(test_basic_string)
{
    {
        const toml::string str("basic string");
        std::ostringstream oss;
        oss << str;
        BOOST_TEST(oss.str() == "\"basic string\"");
    }
    {
        const std::string  s1 ("basic string");
        const toml::string str(s1);
        std::ostringstream oss;
        oss << str;
        BOOST_TEST(oss.str() == "\"basic string\"");
    }
    {
        const toml::string str("basic string", toml::string_t::basic);
        std::ostringstream oss;
        oss << str;
        BOOST_TEST(oss.str() == "\"basic string\"");
    }
    {
        const std::string  s1 ("basic string");
        const toml::string str(s1, toml::string_t::basic);
        std::ostringstream oss;
        oss << str;
        BOOST_TEST(oss.str() == "\"basic string\"");
    }
}

BOOST_AUTO_TEST_CASE(test_basic_ml_string)
{
    {
        const toml::string str("basic\nstring");
        std::ostringstream oss1;
        oss1 << str;
        std::ostringstream oss2;
        oss2 << "\"\"\"\nbasic\nstring\\\n\"\"\"";
        BOOST_TEST(oss1.str() == oss2.str());
    }
    {
        const std::string  s1 ("basic\nstring");
        const toml::string str(s1);
        std::ostringstream oss1;
        oss1 << str;
        std::ostringstream oss2;
        oss2 << "\"\"\"\nbasic\nstring\\\n\"\"\"";
        BOOST_TEST(oss1.str() == oss2.str());
    }
    {
        const toml::string str("basic\nstring", toml::string_t::basic);
        std::ostringstream oss1;
        oss1 << str;
        std::ostringstream oss2;
        oss2 << "\"\"\"\nbasic\nstring\\\n\"\"\"";
        BOOST_TEST(oss1.str() == oss2.str());

    }
    {
        const std::string  s1 ("basic\nstring");
        const toml::string str(s1, toml::string_t::basic);
        std::ostringstream oss1;
        oss1 << str;
        std::ostringstream oss2;
        oss2 << "\"\"\"\nbasic\nstring\\\n\"\"\"";
        BOOST_TEST(oss1.str() == oss2.str());
    }
}


BOOST_AUTO_TEST_CASE(test_literal_string)
{
    {
        const toml::string str("literal string", toml::string_t::literal);
        std::ostringstream oss;
        oss << str;
        BOOST_TEST(oss.str() == "'literal string'");
    }
    {
        const std::string  s1 ("literal string");
        const toml::string str(s1, toml::string_t::literal);
        std::ostringstream oss;
        oss << str;
        BOOST_TEST(oss.str() == "'literal string'");
    }
}

BOOST_AUTO_TEST_CASE(test_literal_ml_string)
{
    {
        const toml::string str("literal\nstring", toml::string_t::literal);
        std::ostringstream oss1;
        oss1 << str;
        std::ostringstream oss2;
        oss2 << "'''\nliteral\nstring'''";
        BOOST_TEST(oss1.str() == oss2.str());

    }
    {
        const std::string  s1 ("literal\nstring");
        const toml::string str(s1, toml::string_t::literal);
        std::ostringstream oss1;
        oss1 << str;
        std::ostringstream oss2;
        oss2 << "'''\nliteral\nstring'''";
        BOOST_TEST(oss1.str() == oss2.str());
    }
}

BOOST_AUTO_TEST_CASE(test_string_add_assign)
{
    // string literal
    {
        toml::string str("foo");
        str += "bar";
        BOOST_TEST(str.str == "foobar");
    }
    // std::string
    {
        toml::string str("foo");
        std::string str2("bar");
        str += str2;
        BOOST_TEST(str.str == "foobar");
    }
    // toml::string
    {
        toml::string str("foo");
        toml::string str2("bar");
        str += str2;
        BOOST_TEST(str.str == "foobar");
    }
#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
    // std::string_view
    {
        toml::string str("foo");
        str += std::string_view("bar");
        BOOST_TEST(str == "foobar");
    }
#endif
    // std::string += toml::string
    {
        std::string  str("foo");
        toml::string str2("bar");
        str += str2;
        BOOST_TEST(str == "foobar");
    }


}
