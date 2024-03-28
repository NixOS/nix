#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

#include <cmath>

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_fractional)
{
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1.0",                1.0);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "0.1",                0.1);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "0.001",              0.001);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "0.100",              0.1);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+3.14",              3.14);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-3.14",             -3.14);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "3.1415_9265_3589",   3.141592653589);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+3.1415_9265_3589",  3.141592653589);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-3.1415_9265_3589", -3.141592653589);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "123_456.789",        123456.789);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+123_456.789",       123456.789);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-123_456.789",      -123456.789);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+0.0",               0.0);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-0.0",              -0.0);
}

BOOST_AUTO_TEST_CASE(test_fractional_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1.0",               value( 1.0));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0.1",               value( 0.1));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0.001",             value( 0.001));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "0.100",             value( 0.1));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+3.14",             value( 3.14));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-3.14",             value(-3.14));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "3.1415_9265_3589",  value( 3.141592653589));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+3.1415_9265_3589", value( 3.141592653589));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-3.1415_9265_3589", value(-3.141592653589));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "123_456.789",       value( 123456.789));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+123_456.789",      value( 123456.789));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-123_456.789",      value(-123456.789));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+0.0",              value( 0.0));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-0.0",              value(-0.0));
}

BOOST_AUTO_TEST_CASE(test_exponential)
{
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1e10",       1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1e+10",      1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1e-10",      1e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+1e10",      1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+1e+10",     1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+1e-10",     1e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-1e10",      -1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-1e+10",     -1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-1e-10",     -1e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "123e-10",    123e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1E10",       1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1E+10",      1e10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1E-10",      1e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "123E-10",    123e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1_2_3E-10",  123e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1_2_3E-1_0", 123e-10);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "+0e0",        0.0);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "-0e0",       -0.0);

#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
    BOOST_TEST_MESSAGE("testing an unreleased toml feature: leading zeroes in float exponent part");
    // toml-lang/toml master permits leading 0s in exp part (unreleased)
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1_2_3E-01",  123e-1);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1_2_3E-0_1", 123e-1);
#endif
}

BOOST_AUTO_TEST_CASE(test_exponential_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1e10",       value(1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1e+10",      value(1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1e-10",      value(1e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+1e10",      value(1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+1e+10",     value(1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+1e-10",     value(1e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-1e10",      value(-1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-1e+10",     value(-1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-1e-10",     value(-1e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "123e-10",    value(123e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1E10",       value(1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1E+10",      value(1e10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1E-10",      value(1e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "123E-10",    value(123e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1_2_3E-10",  value(123e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1_2_3E-1_0", value(123e-10));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "+0e0",       value( 0.0));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "-0e0",       value(-0.0));

#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
    BOOST_TEST_MESSAGE("testing an unreleased toml feature: leading zeroes in float exponent part");
    // toml-lang/toml master permits leading 0s in exp part (unreleased)
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1_2_3E-01",  value(123e-1));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1_2_3E-0_1", value(123e-1));
#endif
}
BOOST_AUTO_TEST_CASE(test_fe)
{
    TOML11_TEST_PARSE_EQUAL(parse_floating, "6.02e23",          6.02e23);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "6.02e+23",         6.02e23);
    TOML11_TEST_PARSE_EQUAL(parse_floating, "1.112_650_06e-17", 1.11265006e-17);
}
BOOST_AUTO_TEST_CASE(test_fe_vaule)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "6.02e23",          value(6.02e23));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "6.02e+23",         value(6.02e23));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1.112_650_06e-17", value(1.11265006e-17));

#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
    BOOST_TEST_MESSAGE("testing an unreleased toml feature: leading zeroes in float exponent part");
    // toml-lang/toml master permits leading 0s in exp part (unreleased)
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "3.141_5e-01",      value(3.1415e-1));
#endif
}

BOOST_AUTO_TEST_CASE(test_inf)
{
    {
        const std::string token("inf");
        toml::detail::location loc("test", token);
        const auto r = parse_floating(loc);
        BOOST_CHECK(r.is_ok());
        BOOST_CHECK(std::isinf(r.unwrap().first));
        BOOST_CHECK(r.unwrap().first > 0.0);
    }
    {
        const std::string token("+inf");
        toml::detail::location loc("test", token);
        const auto r = parse_floating(loc);
        BOOST_CHECK(r.is_ok());
        BOOST_CHECK(std::isinf(r.unwrap().first));
        BOOST_CHECK(r.unwrap().first > 0.0);
    }
    {
        const std::string token("-inf");
        toml::detail::location loc("test", token);
        const auto r = parse_floating(loc);
        BOOST_CHECK(r.is_ok());
        BOOST_CHECK(std::isinf(r.unwrap().first));
        BOOST_CHECK(r.unwrap().first < 0.0);
    }
}

BOOST_AUTO_TEST_CASE(test_nan)
{
    {
        const std::string token("nan");
        toml::detail::location loc("test", token);
        const auto r = parse_floating(loc);
        BOOST_CHECK(r.is_ok());
        BOOST_CHECK(std::isnan(r.unwrap().first));
    }
    {
        const std::string token("+nan");
        toml::detail::location loc("test", token);
        const auto r = parse_floating(loc);
        BOOST_CHECK(r.is_ok());
        BOOST_CHECK(std::isnan(r.unwrap().first));
    }
    {
        const std::string token("-nan");
        toml::detail::location loc("test", token);
        const auto r = parse_floating(loc);
        BOOST_CHECK(r.is_ok());
        BOOST_CHECK(std::isnan(r.unwrap().first));
    }
}

BOOST_AUTO_TEST_CASE(test_overflow)
{
    std::istringstream float_overflow (std::string("float-overflow  = 1.0e+1024"));
    BOOST_CHECK_THROW(toml::parse(float_overflow ), toml::syntax_error);
    // istringstream >> float does not set failbit in case of underflow.
}
