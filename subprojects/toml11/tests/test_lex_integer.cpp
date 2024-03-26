#include <toml/lexer.hpp>

#include "unit_test.hpp"
#include "test_lex_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_decimal_correct)
{
    TOML11_TEST_LEX_ACCEPT(lex_integer, "1234",        "1234"       );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "+1234",       "+1234"      );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "-1234",       "-1234"      );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0",           "0"          );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "1_2_3_4",     "1_2_3_4"    );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "+1_2_3_4",    "+1_2_3_4"   );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "-1_2_3_4",    "-1_2_3_4"   );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "123_456_789", "123_456_789");
}

BOOST_AUTO_TEST_CASE(test_decimal_invalid)
{
    TOML11_TEST_LEX_ACCEPT(lex_integer, "123+45",  "123");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "123-45",  "123");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "01234",   "0");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "123__45", "123");

    TOML11_TEST_LEX_REJECT(lex_integer, "_1234");
}

BOOST_AUTO_TEST_CASE(test_hex_correct)
{
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xDEADBEEF",  "0xDEADBEEF" );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xdeadbeef",  "0xdeadbeef" );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xDEADbeef",  "0xDEADbeef" );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xDEAD_BEEF", "0xDEAD_BEEF");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xdead_beef", "0xdead_beef");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xdead_BEEF", "0xdead_BEEF");

    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xFF",     "0xFF"    );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0x00FF",   "0x00FF"  );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0x0000FF", "0x0000FF");
}

BOOST_AUTO_TEST_CASE(test_hex_invalid)
{
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xAPPLE",     "0xA");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xDEAD+BEEF", "0xDEAD");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0xDEAD__BEEF", "0xDEAD");

    TOML11_TEST_LEX_REJECT(lex_hex_int, "0x_DEADBEEF");
    TOML11_TEST_LEX_REJECT(lex_hex_int, "0x+DEADBEEF");
    TOML11_TEST_LEX_REJECT(lex_hex_int, "-0xFF"      );
    TOML11_TEST_LEX_REJECT(lex_hex_int, "-0x00FF"    );

    TOML11_TEST_LEX_ACCEPT(lex_integer, "0x_DEADBEEF", "0" );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0x+DEADBEEF", "0" );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "-0xFF"      , "-0" );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "-0x00FF"    , "-0" );
}

BOOST_AUTO_TEST_CASE(test_oct_correct)
{
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o777",    "0o777"  );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o7_7_7",  "0o7_7_7");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o007",    "0o007"  );

}

BOOST_AUTO_TEST_CASE(test_oct_invalid)
{
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o77+7", "0o77");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o1__0", "0o1");

    TOML11_TEST_LEX_REJECT(lex_oct_int, "0o800" );
    TOML11_TEST_LEX_REJECT(lex_oct_int, "-0o777");
    TOML11_TEST_LEX_REJECT(lex_oct_int, "0o+777");
    TOML11_TEST_LEX_REJECT(lex_oct_int, "0o_10" );

    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o800",  "0");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "-0o777", "-0");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o+777", "0");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0o_10",  "0");
}

BOOST_AUTO_TEST_CASE(test_bin_correct)
{
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0b10000",    "0b10000"   );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0b010000",   "0b010000"  );
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0b01_00_00", "0b01_00_00");
    TOML11_TEST_LEX_ACCEPT(lex_integer, "0b111111",   "0b111111"  );
}

BOOST_AUTO_TEST_CASE(test_bin_invalid)
{
    TOML11_TEST_LEX_ACCEPT(lex_bin_int, "0b11__11", "0b11");
    TOML11_TEST_LEX_ACCEPT(lex_bin_int, "0b11+11" , "0b11");

    TOML11_TEST_LEX_REJECT(lex_bin_int, "-0b10000");
    TOML11_TEST_LEX_REJECT(lex_bin_int, "0b_1111" );
}
