#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_decimal)
{
    TOML11_TEST_PARSE_EQUAL(parse_integer,        "1234",       1234);
    TOML11_TEST_PARSE_EQUAL(parse_integer,       "+1234",       1234);
    TOML11_TEST_PARSE_EQUAL(parse_integer,       "-1234",      -1234);
    TOML11_TEST_PARSE_EQUAL(parse_integer,           "0",          0);
    TOML11_TEST_PARSE_EQUAL(parse_integer,     "1_2_3_4",       1234);
    TOML11_TEST_PARSE_EQUAL(parse_integer,    "+1_2_3_4",      +1234);
    TOML11_TEST_PARSE_EQUAL(parse_integer,    "-1_2_3_4",      -1234);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "123_456_789",  123456789);
}

BOOST_AUTO_TEST_CASE(test_decimal_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,        "1234", toml::value(     1234));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,       "+1234", toml::value(     1234));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,       "-1234", toml::value(    -1234));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,           "0", toml::value(        0));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,     "1_2_3_4", toml::value(     1234));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,    "+1_2_3_4", toml::value(    +1234));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,    "-1_2_3_4", toml::value(    -1234));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "123_456_789", toml::value(123456789));
}

BOOST_AUTO_TEST_CASE(test_hex)
{
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0xDEADBEEF",  0xDEADBEEF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0xdeadbeef",  0xDEADBEEF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0xDEADbeef",  0xDEADBEEF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0xDEAD_BEEF", 0xDEADBEEF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0xdead_beef", 0xDEADBEEF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0xdead_BEEF", 0xDEADBEEF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0xFF",        0xFF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0x00FF",      0xFF);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0x0000FF",    0xFF);
}

BOOST_AUTO_TEST_CASE(test_hex_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0xDEADBEEF",  value(0xDEADBEEF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0xdeadbeef",  value(0xDEADBEEF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0xDEADbeef",  value(0xDEADBEEF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0xDEAD_BEEF", value(0xDEADBEEF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0xdead_beef", value(0xDEADBEEF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0xdead_BEEF", value(0xDEADBEEF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0xFF",        value(0xFF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0x00FF",      value(0xFF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0x0000FF",    value(0xFF));
}

BOOST_AUTO_TEST_CASE(test_oct)
{
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0o777",   64*7+8*7+7);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0o7_7_7", 64*7+8*7+7);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0o007",   7);
}

BOOST_AUTO_TEST_CASE(test_oct_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0o777",   value(64*7+8*7+7));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0o7_7_7", value(64*7+8*7+7));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0o007",   value(7));
}

BOOST_AUTO_TEST_CASE(test_bin)
{
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0b10000",    16);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0b010000",   16);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0b01_00_00", 16);
    TOML11_TEST_PARSE_EQUAL(parse_integer, "0b111111",   63);
}

BOOST_AUTO_TEST_CASE(test_bin_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0b10000",    value(16));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0b010000",   value(16));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0b01_00_00", value(16));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0b111111",   value(63));

    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "0b1000_1000_1000_1000_1000_1000_1000_1000_1000_1000_1000_1000_1000_1000_1000",
        //      1   0   0   0
        //      0   C   8   4
        value(0x0888888888888888));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "0b01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111",
        //      1   0   0   0
        //      0   C   8   4
        value(0x7FFFFFFFFFFFFFFF));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "0b00000000_01111111_11111111_11111111_11111111_11111111_11111111_11111111_11111111",
        //      1   0   0   0
        //      0   C   8   4
        value(0x7FFFFFFFFFFFFFFF));
}

BOOST_AUTO_TEST_CASE(test_integer_overflow)
{
    std::istringstream dec_overflow(std::string("dec-overflow = 9223372036854775808"));
    std::istringstream hex_overflow(std::string("hex-overflow = 0x1_00000000_00000000"));
    std::istringstream oct_overflow(std::string("oct-overflow = 0o1_000_000_000_000_000_000_000"));
    //                                                           64       56       48       40       32       24       16        8
    std::istringstream bin_overflow(std::string("bin-overflow = 0b10000000_00000000_00000000_00000000_00000000_00000000_00000000_00000000"));
    BOOST_CHECK_THROW(toml::parse(dec_overflow), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(hex_overflow), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(oct_overflow), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(bin_overflow), toml::syntax_error);
}
