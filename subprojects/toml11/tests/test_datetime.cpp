#include <toml/datetime.hpp>

#include "unit_test.hpp"

BOOST_AUTO_TEST_CASE(test_local_date)
{
    const toml::local_date date(2018, toml::month_t::Jan, 1);
    const toml::local_date date1(date);
    BOOST_TEST(date == date1);

    const std::chrono::system_clock::time_point tp(date);
    const toml::local_date date2(tp);
    BOOST_TEST(date == date2);

    const toml::local_date date3(2017, toml::month_t::Dec, 31);
    BOOST_TEST(date > date3);

    std::ostringstream oss;
    oss << date;
    BOOST_TEST(oss.str() == std::string("2018-01-01"));
}

BOOST_AUTO_TEST_CASE(test_local_time)
{
    const toml::local_time time(12, 30, 45);
    const toml::local_time time1(time);
    BOOST_TEST(time == time1);

    const std::chrono::nanoseconds dur(time);
    std::chrono::nanoseconds ns(0);
    ns += std::chrono::hours  (12);
    ns += std::chrono::minutes(30);
    ns += std::chrono::seconds(45);
    BOOST_TEST(dur.count() == ns.count());

    const toml::local_time time3(12, 15, 45);
    BOOST_TEST(time > time3);

    {
        std::ostringstream oss;
        oss << time;
        BOOST_TEST(oss.str() == std::string("12:30:45"));
    }

    {
        const toml::local_time time4(12, 30, 45, 123, 456);
        std::ostringstream oss;
        oss << time4;
        BOOST_TEST(oss.str() == std::string("12:30:45.123456"));
    }
}

BOOST_AUTO_TEST_CASE(test_time_offset)
{
    const toml::time_offset time(9, 30);
    const toml::time_offset time1(time);
    BOOST_TEST(time == time1);

    const std::chrono::minutes dur(time);
    std::chrono::minutes m(0);
    m += std::chrono::hours  (9);
    m += std::chrono::minutes(30);
    BOOST_TEST(dur.count() == m.count());

    const toml::time_offset time2(9, 0);
    BOOST_TEST(time2 < time);

    std::ostringstream oss;
    oss << time;
    BOOST_TEST(oss.str() == std::string("+09:30"));
}

BOOST_AUTO_TEST_CASE(test_local_datetime)
{
    const toml::local_datetime dt(toml::local_date(2018, toml::month_t::Jan, 1),
                                  toml::local_time(12, 30, 45));
    const toml::local_datetime dt1(dt);
    BOOST_TEST(dt == dt1);

    const std::chrono::system_clock::time_point tp(dt);
    const toml::local_datetime dt2(tp);
    BOOST_TEST(dt == dt2);

    std::ostringstream oss;
    oss << dt;
    BOOST_TEST(oss.str() == std::string("2018-01-01T12:30:45"));
}

BOOST_AUTO_TEST_CASE(test_offset_datetime)
{
    const toml::offset_datetime dt(toml::local_date(2018, toml::month_t::Jan, 1),
                                   toml::local_time(12, 30, 45),
                                   toml::time_offset(9, 30));
    const toml::offset_datetime dt1(dt);
    BOOST_TEST(dt == dt1);

    const std::chrono::system_clock::time_point tp1(dt);
    const toml::offset_datetime dt2(tp1);
    const std::chrono::system_clock::time_point tp2(dt2);
    const bool tp_same = (tp1 == tp2);
    BOOST_TEST(tp_same);

    {
        std::ostringstream oss;
        oss << dt;
        BOOST_TEST(oss.str() == std::string("2018-01-01T12:30:45+09:30"));
    }
    {
        const toml::offset_datetime dt3(
                toml::local_date(2018, toml::month_t::Jan, 1),
                toml::local_time(12, 30, 45),
                toml::time_offset(0, 0));
        std::ostringstream oss;
        oss << dt3;
        BOOST_TEST(oss.str() == std::string("2018-01-01T12:30:45Z"));
    }
}
