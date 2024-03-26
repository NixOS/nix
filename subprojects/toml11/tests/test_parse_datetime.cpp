#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_time)
{
    TOML11_TEST_PARSE_EQUAL(parse_local_time, "07:32:00",        toml::local_time(7, 32, 0));
    TOML11_TEST_PARSE_EQUAL(parse_local_time, "07:32:00.99",     toml::local_time(7, 32, 0, 990, 0));
    TOML11_TEST_PARSE_EQUAL(parse_local_time, "07:32:00.999",    toml::local_time(7, 32, 0, 999, 0));
    TOML11_TEST_PARSE_EQUAL(parse_local_time, "07:32:00.999999", toml::local_time(7, 32, 0, 999, 999));


    TOML11_TEST_PARSE_EQUAL(parse_local_time, "00:00:00.000000", toml::local_time( 0,  0,  0,   0,   0));
    TOML11_TEST_PARSE_EQUAL(parse_local_time, "23:59:59.999999", toml::local_time(23, 59, 59, 999, 999));
    TOML11_TEST_PARSE_EQUAL(parse_local_time, "23:59:60.999999", toml::local_time(23, 59, 60, 999, 999)); // leap second
}

BOOST_AUTO_TEST_CASE(test_time_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "07:32:00",        toml::value(toml::local_time(7, 32, 0)));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "07:32:00.99",     toml::value(toml::local_time(7, 32, 0, 990, 0)));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "07:32:00.999",    toml::value(toml::local_time(7, 32, 0, 999, 0)));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "07:32:00.999999", toml::value(toml::local_time(7, 32, 0, 999, 999)));

    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "00:00:00.000000", toml::value(toml::local_time( 0,  0,  0,   0,   0)));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "23:59:59.999999", toml::value(toml::local_time(23, 59, 59, 999, 999)));

    std::istringstream stream1(std::string("invalid-datetime = 24:00:00"));
    std::istringstream stream2(std::string("invalid-datetime = 00:60:00"));
    std::istringstream stream3(std::string("invalid-datetime = 00:00:61"));
    BOOST_CHECK_THROW(toml::parse(stream1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream2), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream3), toml::syntax_error);
}

BOOST_AUTO_TEST_CASE(test_date)
{
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "1979-05-27", toml::local_date(1979, toml::month_t::May, 27));

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-01-01", toml::local_date(2000, toml::month_t::Jan,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-01-31", toml::local_date(2000, toml::month_t::Jan, 31));
    std::istringstream stream1_1(std::string("invalid-datetime = 2000-01-00"));
    std::istringstream stream1_2(std::string("invalid-datetime = 2000-01-32"));
    BOOST_CHECK_THROW(toml::parse(stream1_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream1_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-02-01", toml::local_date(2000, toml::month_t::Feb,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-02-29", toml::local_date(2000, toml::month_t::Feb, 29));
    std::istringstream stream2_1(std::string("invalid-datetime = 2000-02-00"));
    std::istringstream stream2_2(std::string("invalid-datetime = 2000-02-30"));
    BOOST_CHECK_THROW(toml::parse(stream2_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream2_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2001-02-28", toml::local_date(2001, toml::month_t::Feb, 28));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2004-02-29", toml::local_date(2004, toml::month_t::Feb, 29));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2100-02-28", toml::local_date(2100, toml::month_t::Feb, 28));
    std::istringstream stream2_3(std::string("invalid-datetime = 2001-02-29"));
    std::istringstream stream2_4(std::string("invalid-datetime = 2004-02-30"));
    std::istringstream stream2_5(std::string("invalid-datetime = 2100-02-29"));
    BOOST_CHECK_THROW(toml::parse(stream2_3), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream2_4), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream2_5), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-03-01", toml::local_date(2000, toml::month_t::Mar,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-03-31", toml::local_date(2000, toml::month_t::Mar, 31));
    std::istringstream stream3_1(std::string("invalid-datetime = 2000-03-00"));
    std::istringstream stream3_2(std::string("invalid-datetime = 2000-03-32"));
    BOOST_CHECK_THROW(toml::parse(stream3_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream3_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-04-01", toml::local_date(2000, toml::month_t::Apr,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-04-30", toml::local_date(2000, toml::month_t::Apr, 30));
    std::istringstream stream4_1(std::string("invalid-datetime = 2000-04-00"));
    std::istringstream stream4_2(std::string("invalid-datetime = 2000-04-31"));
    BOOST_CHECK_THROW(toml::parse(stream4_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream4_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-05-01", toml::local_date(2000, toml::month_t::May,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-05-31", toml::local_date(2000, toml::month_t::May, 31));
    std::istringstream stream5_1(std::string("invalid-datetime = 2000-05-00"));
    std::istringstream stream5_2(std::string("invalid-datetime = 2000-05-32"));
    BOOST_CHECK_THROW(toml::parse(stream5_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream5_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-06-01", toml::local_date(2000, toml::month_t::Jun,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-06-30", toml::local_date(2000, toml::month_t::Jun, 30));
    std::istringstream stream6_1(std::string("invalid-datetime = 2000-06-00"));
    std::istringstream stream6_2(std::string("invalid-datetime = 2000-06-31"));
    BOOST_CHECK_THROW(toml::parse(stream6_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream6_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-07-01", toml::local_date(2000, toml::month_t::Jul,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-07-31", toml::local_date(2000, toml::month_t::Jul, 31));
    std::istringstream stream7_1(std::string("invalid-datetime = 2000-07-00"));
    std::istringstream stream7_2(std::string("invalid-datetime = 2000-07-32"));
    BOOST_CHECK_THROW(toml::parse(stream7_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream7_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-08-01", toml::local_date(2000, toml::month_t::Aug,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-08-31", toml::local_date(2000, toml::month_t::Aug, 31));
    std::istringstream stream8_1(std::string("invalid-datetime = 2000-08-00"));
    std::istringstream stream8_2(std::string("invalid-datetime = 2000-08-32"));
    BOOST_CHECK_THROW(toml::parse(stream8_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream8_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-09-01", toml::local_date(2000, toml::month_t::Sep,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-09-30", toml::local_date(2000, toml::month_t::Sep, 30));
    std::istringstream stream9_1(std::string("invalid-datetime = 2000-09-00"));
    std::istringstream stream9_2(std::string("invalid-datetime = 2000-09-31"));
    BOOST_CHECK_THROW(toml::parse(stream9_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream9_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-10-01", toml::local_date(2000, toml::month_t::Oct,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-10-31", toml::local_date(2000, toml::month_t::Oct, 31));
    std::istringstream stream10_1(std::string("invalid-datetime = 2000-10-00"));
    std::istringstream stream10_2(std::string("invalid-datetime = 2000-10-32"));
    BOOST_CHECK_THROW(toml::parse(stream10_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream10_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-11-01", toml::local_date(2000, toml::month_t::Nov,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-11-30", toml::local_date(2000, toml::month_t::Nov, 30));
    std::istringstream stream11_1(std::string("invalid-datetime = 2000-11-00"));
    std::istringstream stream11_2(std::string("invalid-datetime = 2000-11-31"));
    BOOST_CHECK_THROW(toml::parse(stream11_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream11_2), toml::syntax_error);

    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-12-01", toml::local_date(2000, toml::month_t::Dec,  1));
    TOML11_TEST_PARSE_EQUAL(parse_local_date,  "2000-12-31", toml::local_date(2000, toml::month_t::Dec, 31));
    std::istringstream stream12_1(std::string("invalid-datetime = 2000-12-00"));
    std::istringstream stream12_2(std::string("invalid-datetime = 2000-12-32"));
    BOOST_CHECK_THROW(toml::parse(stream12_1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream12_2), toml::syntax_error);

    std::istringstream stream13_1(std::string("invalid-datetime = 2000-13-01"));
    BOOST_CHECK_THROW(toml::parse(stream13_1), toml::syntax_error);
    std::istringstream stream0_1(std::string("invalid-datetime = 2000-00-01"));
    BOOST_CHECK_THROW(toml::parse(stream0_1), toml::syntax_error);
}

BOOST_AUTO_TEST_CASE(test_date_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27", value(toml::local_date(1979, toml::month_t::May, 27)));
}

BOOST_AUTO_TEST_CASE(test_datetime)
{
    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27T07:32:00",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27T07:32:00.99",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 990, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27T07:32:00.999999",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 999, 999)));

    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27t07:32:00",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27t07:32:00.99",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 990, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27t07:32:00.999999",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 999, 999)));

    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27 07:32:00",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27 07:32:00.99",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 990, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_local_datetime, "1979-05-27 07:32:00.999999",
        toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 999, 999)));
}

BOOST_AUTO_TEST_CASE(test_datetime_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00.99",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 990, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00.999999",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 999, 999))));

    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27t07:32:00",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27t07:32:00.99",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 990, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27t07:32:00.999999",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 999, 999))));

    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27 07:32:00",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27 07:32:00.99",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 990, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27 07:32:00.999999",
        toml::value(toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27), toml::local_time(7, 32, 0, 999, 999))));
}

BOOST_AUTO_TEST_CASE(test_offset_datetime)
{
    TOML11_TEST_PARSE_EQUAL(parse_offset_datetime, "1979-05-27T07:32:00Z",
        toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_offset_datetime, "1979-05-27T07:32:00.99Z",
        toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 990, 0), toml::time_offset(0, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_offset_datetime, "1979-05-27T07:32:00.999999Z",
        toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 999, 999), toml::time_offset(0, 0)));

    TOML11_TEST_PARSE_EQUAL(parse_offset_datetime, "1979-05-27T07:32:00+09:00",
        toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0), toml::time_offset(9, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_offset_datetime, "1979-05-27T07:32:00.99+09:00",
        toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 990, 0), toml::time_offset(9, 0)));
    TOML11_TEST_PARSE_EQUAL(parse_offset_datetime, "1979-05-27T07:32:00.999999+09:00",
        toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 999, 999), toml::time_offset(9, 0)));

    std::istringstream stream1(std::string("invalid-datetime = 2000-01-01T00:00:00+24:00"));
    std::istringstream stream2(std::string("invalid-datetime = 2000-01-01T00:00:00+00:60"));
    BOOST_CHECK_THROW(toml::parse(stream1), toml::syntax_error);
    BOOST_CHECK_THROW(toml::parse(stream2), toml::syntax_error);
}

BOOST_AUTO_TEST_CASE(test_offset_datetime_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00Z",
        toml::value(toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0), toml::time_offset(0, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00.99Z",
        toml::value(toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 990, 0), toml::time_offset(0, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00.999999Z",
        toml::value(toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 999, 999), toml::time_offset(0, 0))));

    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00+09:00",
        toml::value(toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0), toml::time_offset(9, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00.99+09:00",
        toml::value(toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 990, 0), toml::time_offset(9, 0))));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>, "1979-05-27T07:32:00.999999+09:00",
        toml::value(toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                              toml::local_time(7, 32, 0, 999, 999), toml::time_offset(9, 0))));
}
