#include <toml/lexer.hpp>

#include "unit_test.hpp"
#include "test_lex_aux.hpp"

#include <limits>

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_fractional_valid)
{
    TOML11_TEST_LEX_ACCEPT(lex_float, "1.0",               "1.0"              );
    TOML11_TEST_LEX_ACCEPT(lex_float, "0.1",               "0.1"              );
    TOML11_TEST_LEX_ACCEPT(lex_float, "0.001",             "0.001"            );
    TOML11_TEST_LEX_ACCEPT(lex_float, "0.100",             "0.100"            );
    TOML11_TEST_LEX_ACCEPT(lex_float, "+3.14",             "+3.14"            );
    TOML11_TEST_LEX_ACCEPT(lex_float, "-3.14",             "-3.14"            );
    TOML11_TEST_LEX_ACCEPT(lex_float, "3.1415_9265_3589",  "3.1415_9265_3589" );
    TOML11_TEST_LEX_ACCEPT(lex_float, "+3.1415_9265_3589", "+3.1415_9265_3589");
    TOML11_TEST_LEX_ACCEPT(lex_float, "-3.1415_9265_3589", "-3.1415_9265_3589");
    TOML11_TEST_LEX_ACCEPT(lex_float, "123_456.789",       "123_456.789"      );
    TOML11_TEST_LEX_ACCEPT(lex_float, "+123_456.789",      "+123_456.789"     );
    TOML11_TEST_LEX_ACCEPT(lex_float, "-123_456.789",      "-123_456.789"     );
}

BOOST_AUTO_TEST_CASE(test_fractional_invalid)
{
    TOML11_TEST_LEX_REJECT(lex_float, "0.");
    TOML11_TEST_LEX_REJECT(lex_float, ".0");
    TOML11_TEST_LEX_REJECT(lex_float, "01.0");
    TOML11_TEST_LEX_REJECT(lex_float, "3,14");
    TOML11_TEST_LEX_REJECT(lex_float, "+-1.0");
    TOML11_TEST_LEX_REJECT(lex_float, "1._0");
}

BOOST_AUTO_TEST_CASE(test_exponential_valid)
{
    TOML11_TEST_LEX_ACCEPT(lex_float, "1e10",       "1e10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1e+10",      "1e+10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1e-10",      "1e-10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "+1e10",      "+1e10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "+1e+10",     "+1e+10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "+1e-10",     "+1e-10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "-1e10",      "-1e10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "-1e+10",     "-1e+10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "-1e-10",     "-1e-10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "123e-10",    "123e-10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1E10",       "1E10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1E+10",      "1E+10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1E-10",      "1E-10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "123E-10",    "123E-10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1_2_3E-10",  "1_2_3E-10");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1_2_3E-1_0", "1_2_3E-1_0");

#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
    BOOST_TEST_MESSAGE("testing an unreleased toml feature: leading zeroes in float exponent part");
    // toml-lang/toml master permits leading 0s in exp part (unreleased)
    TOML11_TEST_LEX_ACCEPT(lex_float, "1_2_3E-01",  "1_2_3E-01");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1_2_3E-0_1", "1_2_3E-0_1");
#endif
}

BOOST_AUTO_TEST_CASE(test_exponential_invalid)
{
    // accept partially
    TOML11_TEST_LEX_ACCEPT(lex_float, "1e1E0", "1e1");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1E1e0", "1E1");
}

BOOST_AUTO_TEST_CASE(test_both_valid)
{
    TOML11_TEST_LEX_ACCEPT(lex_float, "6.02e23",          "6.02e23");
    TOML11_TEST_LEX_ACCEPT(lex_float, "6.02e+23",         "6.02e+23");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1.112_650_06e-17", "1.112_650_06e-17");

#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
    BOOST_TEST_MESSAGE("testing an unreleased toml feature: leading zeroes in float exponent part");
    // toml-lang/toml master permits leading 0s in exp part (unreleased)
    TOML11_TEST_LEX_ACCEPT(lex_float, "1.0e-07",          "1.0e-07");
#endif
}

BOOST_AUTO_TEST_CASE(test_both_invalid)
{
    TOML11_TEST_LEX_REJECT(lex_float, "01e1.0");
    // accept partially
    TOML11_TEST_LEX_ACCEPT(lex_float, "1e1.0",  "1e1");

#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
    BOOST_TEST_MESSAGE("testing an unreleased toml feature: leading zeroes in float exponent part");
    // toml-lang/toml master permits leading 0s in exp part (unreleased)
    TOML11_TEST_LEX_ACCEPT(lex_float, "1.0e_01",  "1.0");
    TOML11_TEST_LEX_ACCEPT(lex_float, "1.0e0__1", "1.0e0");
#endif
}

BOOST_AUTO_TEST_CASE(test_special_floating_point)
{
    TOML11_TEST_LEX_ACCEPT(lex_float,  "inf",  "inf");
    TOML11_TEST_LEX_ACCEPT(lex_float, "+inf", "+inf");
    TOML11_TEST_LEX_ACCEPT(lex_float, "-inf", "-inf");

    TOML11_TEST_LEX_ACCEPT(lex_float,  "nan",  "nan");
    TOML11_TEST_LEX_ACCEPT(lex_float, "+nan", "+nan");
    TOML11_TEST_LEX_ACCEPT(lex_float, "-nan", "-nan");
}
