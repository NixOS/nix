#include <toml/get.hpp>
#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_normal_table)
{
    std::string table(
        "key1 = \"value\"\n"
        "key2 = 42\n"
        "key3 = 3.14\n"
        );
    location loc("test", table);

    const auto result = toml::detail::parse_ml_table<toml::value>(loc);
    BOOST_TEST(result.is_ok());
    const auto data = result.unwrap();

    BOOST_TEST(toml::get<std::string >(data.at("key1")) == "value");
    BOOST_TEST(toml::get<std::int64_t>(data.at("key2")) == 42);
    BOOST_TEST(toml::get<double      >(data.at("key3")) == 3.14);
}

BOOST_AUTO_TEST_CASE(test_nested_table)
{
    std::string table(
        "a.b   = \"value\"\n"
        "a.c.d = 42\n"
        );
    location loc("test", table);

    const auto result = toml::detail::parse_ml_table<toml::value>(loc);
    BOOST_TEST(result.is_ok());
    const auto data = result.unwrap();

    const auto a = toml::get<toml::table>(data.at("a"));
    const auto c = toml::get<toml::table>(a.at("c"));

    BOOST_TEST(toml::get<std::string >(a.at("b")) == "value");
    BOOST_TEST(toml::get<std::int64_t>(c.at("d")) == 42);
}
