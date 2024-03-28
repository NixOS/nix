#include <toml/lexer.hpp>

#include "unit_test.hpp"
#include "test_lex_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_offset_datetime)
{
    TOML11_TEST_LEX_ACCEPT(lex_offset_date_time,
            "1979-05-27T07:32:00Z",
            "1979-05-27T07:32:00Z");
    TOML11_TEST_LEX_ACCEPT(lex_offset_date_time,
            "1979-05-27T07:32:00-07:00",
            "1979-05-27T07:32:00-07:00");
    TOML11_TEST_LEX_ACCEPT(lex_offset_date_time,
            "1979-05-27T07:32:00.999999-07:00",
            "1979-05-27T07:32:00.999999-07:00");

    TOML11_TEST_LEX_ACCEPT(lex_offset_date_time,
            "1979-05-27 07:32:00Z",
            "1979-05-27 07:32:00Z");
    TOML11_TEST_LEX_ACCEPT(lex_offset_date_time,
            "1979-05-27 07:32:00-07:00",
            "1979-05-27 07:32:00-07:00");
    TOML11_TEST_LEX_ACCEPT(lex_offset_date_time,
            "1979-05-27 07:32:00.999999-07:00",
            "1979-05-27 07:32:00.999999-07:00");
}

BOOST_AUTO_TEST_CASE(test_local_datetime)
{
    TOML11_TEST_LEX_ACCEPT(lex_local_date_time,
            "1979-05-27T07:32:00",
            "1979-05-27T07:32:00");
    TOML11_TEST_LEX_ACCEPT(lex_local_date_time,
            "1979-05-27T07:32:00.999999",
            "1979-05-27T07:32:00.999999");

    TOML11_TEST_LEX_ACCEPT(lex_local_date_time,
            "1979-05-27 07:32:00",
            "1979-05-27 07:32:00");
    TOML11_TEST_LEX_ACCEPT(lex_local_date_time,
            "1979-05-27 07:32:00.999999",
            "1979-05-27 07:32:00.999999");
}

BOOST_AUTO_TEST_CASE(test_local_date)
{
    TOML11_TEST_LEX_ACCEPT(lex_local_date, "1979-05-27", "1979-05-27");
}
BOOST_AUTO_TEST_CASE(test_local_time)
{
    TOML11_TEST_LEX_ACCEPT(lex_local_time, "07:32:00", "07:32:00");
    TOML11_TEST_LEX_ACCEPT(lex_local_time, "07:32:00.999999", "07:32:00.999999");
}
