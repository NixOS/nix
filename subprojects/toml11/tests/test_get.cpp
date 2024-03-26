#include <toml.hpp>

#include "unit_test.hpp"

#include <array>
#include <deque>
#include <list>
#include <map>
#include <tuple>
#include <unordered_map>

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
#include <string_view>
#endif

using test_value_types = std::tuple<
    toml::basic_value<toml::discard_comments>,
    toml::basic_value<toml::preserve_comments>,
    toml::basic_value<toml::discard_comments,  std::map, std::deque>,
    toml::basic_value<toml::preserve_comments, std::map, std::deque>
>;

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_exact, value_type, test_value_types)
{
    {
        value_type v(true);
        BOOST_TEST(true == toml::get<toml::boolean>(v));

        toml::get<toml::boolean>(v) = false;
        BOOST_TEST(false == toml::get<toml::boolean>(v));

        toml::boolean x = toml::get<toml::boolean>(std::move(v));
        BOOST_TEST(false == x);
    }
    {
        value_type v(42);
        BOOST_TEST(toml::integer(42) == toml::get<toml::integer>(v));

        toml::get<toml::integer>(v) = 54;
        BOOST_TEST(toml::integer(54) == toml::get<toml::integer>(v));

        toml::integer x = toml::get<toml::integer>(std::move(v));
        BOOST_TEST(toml::integer(54) == x);
    }
    {
        value_type v(3.14);
        BOOST_TEST(toml::floating(3.14) == toml::get<toml::floating>(v));

        toml::get<toml::floating>(v) = 2.71;
        BOOST_TEST(toml::floating(2.71) == toml::get<toml::floating>(v));

        toml::floating x = toml::get<toml::floating>(std::move(v));
        BOOST_TEST(toml::floating(2.71) == x);
    }
    {
        value_type v("foo");
        BOOST_TEST(toml::string("foo", toml::string_t::basic) ==
                          toml::get<toml::string>(v));

        toml::get<toml::string>(v).str += "bar";
        BOOST_TEST(toml::string("foobar", toml::string_t::basic) ==
                          toml::get<toml::string>(v));

        toml::string x = toml::get<toml::string>(std::move(v));
        BOOST_TEST(toml::string("foobar") == x);
    }
    {
        value_type v("foo", toml::string_t::literal);
        BOOST_TEST(toml::string("foo", toml::string_t::literal) ==
                          toml::get<toml::string>(v));

        toml::get<toml::string>(v).str += "bar";
        BOOST_TEST(toml::string("foobar", toml::string_t::literal) ==
                          toml::get<toml::string>(v));

        toml::string x = toml::get<toml::string>(std::move(v));
        BOOST_TEST(toml::string("foobar", toml::string_t::literal) == x);
    }
    {
        toml::local_date d(2018, toml::month_t::Apr, 22);
        value_type v(d);
        BOOST_TEST(d == toml::get<toml::local_date>(v));

        toml::get<toml::local_date>(v).year = 2017;
        d.year = 2017;
        BOOST_TEST(d == toml::get<toml::local_date>(v));

        toml::local_date x = toml::get<toml::local_date>(std::move(v));
        BOOST_TEST(d == x);
    }
    {
        toml::local_time t(12, 30, 45);
        value_type v(t);
        BOOST_TEST(t == toml::get<toml::local_time>(v));

        toml::get<toml::local_time>(v).hour = 9;
        t.hour = 9;
        BOOST_TEST(t == toml::get<toml::local_time>(v));

        toml::local_time x = toml::get<toml::local_time>(std::move(v));
        BOOST_TEST(t == x);
    }
    {
        toml::local_datetime dt(toml::local_date(2018, toml::month_t::Apr, 22),
                                toml::local_time(12, 30, 45));
        value_type v(dt);
        BOOST_TEST(dt == toml::get<toml::local_datetime>(v));

        toml::get<toml::local_datetime>(v).date.year = 2017;
        dt.date.year = 2017;
        BOOST_TEST(dt == toml::get<toml::local_datetime>(v));

        toml::local_datetime x = toml::get<toml::local_datetime>(std::move(v));
        BOOST_TEST(dt == x);
    }
    {
        toml::offset_datetime dt(toml::local_datetime(
                    toml::local_date(2018, toml::month_t::Apr, 22),
                    toml::local_time(12, 30, 45)), toml::time_offset(9, 0));
        value_type v(dt);
        BOOST_TEST(dt == toml::get<toml::offset_datetime>(v));

        toml::get<toml::offset_datetime>(v).date.year = 2017;
        dt.date.year = 2017;
        BOOST_TEST(dt == toml::get<toml::offset_datetime>(v));

        toml::offset_datetime x = toml::get<toml::offset_datetime>(std::move(v));
        BOOST_TEST(dt == x);
    }
    {
        using array_type = typename value_type::array_type;
        array_type vec;
        vec.push_back(value_type(42));
        vec.push_back(value_type(54));
        value_type v(vec);
        BOOST_TEST(vec == toml::get<array_type>(v));

        toml::get<array_type>(v).push_back(value_type(123));
        vec.push_back(value_type(123));
        BOOST_TEST(vec == toml::get<array_type>(v));

        array_type x = toml::get<array_type>(std::move(v));
        BOOST_TEST(vec == x);
    }
    {
        using table_type = typename value_type::table_type;
        table_type tab;
        tab["key1"] = value_type(42);
        tab["key2"] = value_type(3.14);
        value_type v(tab);
        BOOST_TEST(tab == toml::get<table_type>(v));

        toml::get<table_type>(v)["key3"] = value_type(123);
        tab["key3"] = value_type(123);
        BOOST_TEST(tab == toml::get<table_type>(v));

        table_type x = toml::get<table_type>(std::move(v));
        BOOST_TEST(tab == x);
    }
    {
        value_type v1(42);
        BOOST_TEST(v1 == toml::get<value_type>(v1));

        value_type v2(54);
        toml::get<value_type>(v1) = v2;
        BOOST_TEST(v2 == toml::get<value_type>(v1));

        value_type x = toml::get<value_type>(std::move(v1));
        BOOST_TEST(v2 == x);
    }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_integer_type, value_type, test_value_types)
{
    {
        value_type v(42);
        BOOST_TEST(int(42) ==           toml::get<int          >(v));
        BOOST_TEST(short(42) ==         toml::get<short        >(v));
        BOOST_TEST(char(42) ==          toml::get<char         >(v));
        BOOST_TEST(unsigned(42) ==      toml::get<unsigned     >(v));
        BOOST_TEST(long(42) ==          toml::get<long         >(v));
        BOOST_TEST(std::int64_t(42) ==  toml::get<std::int64_t >(v));
        BOOST_TEST(std::uint64_t(42) == toml::get<std::uint64_t>(v));
        BOOST_TEST(std::int16_t(42) ==  toml::get<std::int16_t >(v));
        BOOST_TEST(std::uint16_t(42) == toml::get<std::uint16_t>(v));

        BOOST_TEST(std::uint16_t(42) == toml::get<std::uint16_t>(std::move(v)));
    }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_floating_type, value_type, test_value_types)
{
    {
        value_type v(3.14);
        const double ref(3.14);
        BOOST_TEST(static_cast<float      >(ref) == toml::get<float      >(v));
        BOOST_TEST(                         ref  == toml::get<double     >(v));
        BOOST_TEST(static_cast<long double>(ref) == toml::get<long double>(v));
        BOOST_TEST(static_cast<float      >(ref) == toml::get<float>(std::move(v)));
    }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_string_type, value_type, test_value_types)
{
    {
        value_type v("foo", toml::string_t::basic);
        BOOST_TEST("foo" == toml::get<std::string>(v));
        toml::get<std::string>(v) += "bar";
        BOOST_TEST("foobar" == toml::get<std::string>(v));

        const auto x = toml::get<std::string>(std::move(v));
        BOOST_TEST("foobar" == x);
    }
    {
        value_type v("foo", toml::string_t::literal);
        BOOST_TEST("foo" == toml::get<std::string>(v));
        toml::get<std::string>(v) += "bar";
        BOOST_TEST("foobar" == toml::get<std::string>(v));

        const auto x = toml::get<std::string>(std::move(v));
        BOOST_TEST("foobar" == x);
    }

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
    {
        value_type v("foo", toml::string_t::basic);
        BOOST_TEST("foo" == toml::get<std::string_view>(v));
    }
    {
        value_type v("foo", toml::string_t::literal);
        BOOST_TEST("foo" == toml::get<std::string_view>(v));
    }
#endif
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_toml_array, value_type, test_value_types)
{
    {
        const value_type v{42, 54, 69, 72};

        const std::vector<int>         vec = toml::get<std::vector<int>>(v);
        const std::list<short>         lst = toml::get<std::list<short>>(v);
        const std::deque<std::int64_t> deq = toml::get<std::deque<std::int64_t>>(v);

        BOOST_TEST(42 == vec.at(0));
        BOOST_TEST(54 == vec.at(1));
        BOOST_TEST(69 == vec.at(2));
        BOOST_TEST(72 == vec.at(3));

        std::list<short>::const_iterator iter = lst.begin();
        BOOST_TEST(static_cast<short>(42) == *(iter++));
        BOOST_TEST(static_cast<short>(54) == *(iter++));
        BOOST_TEST(static_cast<short>(69) == *(iter++));
        BOOST_TEST(static_cast<short>(72) == *(iter++));

        BOOST_TEST(static_cast<std::int64_t>(42) == deq.at(0));
        BOOST_TEST(static_cast<std::int64_t>(54) == deq.at(1));
        BOOST_TEST(static_cast<std::int64_t>(69) == deq.at(2));
        BOOST_TEST(static_cast<std::int64_t>(72) == deq.at(3));

        std::array<int, 4> ary = toml::get<std::array<int, 4>>(v);
        BOOST_TEST(42 == ary.at(0));
        BOOST_TEST(54 == ary.at(1));
        BOOST_TEST(69 == ary.at(2));
        BOOST_TEST(72 == ary.at(3));

        std::tuple<int, short, unsigned, long> tpl =
            toml::get<std::tuple<int, short, unsigned, long>>(v);
        BOOST_TEST(                      42  == std::get<0>(tpl));
        BOOST_TEST(static_cast<short   >(54) == std::get<1>(tpl));
        BOOST_TEST(static_cast<unsigned>(69) == std::get<2>(tpl));
        BOOST_TEST(static_cast<long    >(72) == std::get<3>(tpl));

        const value_type p{3.14, 2.71};
        std::pair<double, double> pr = toml::get<std::pair<double, double> >(p);
        BOOST_TEST(3.14 == pr.first);
        BOOST_TEST(2.71 == pr.second);
    }

    {
        value_type v{42, 54, 69, 72};
        const std::vector<int> vec = toml::get<std::vector<int>>(std::move(v));
        BOOST_TEST(42 == vec.at(0));
        BOOST_TEST(54 == vec.at(1));
        BOOST_TEST(69 == vec.at(2));
        BOOST_TEST(72 == vec.at(3));
    }
    {
        value_type v{42, 54, 69, 72};
        const std::deque<int> deq = toml::get<std::deque<int>>(std::move(v));
        BOOST_TEST(42 == deq.at(0));
        BOOST_TEST(54 == deq.at(1));
        BOOST_TEST(69 == deq.at(2));
        BOOST_TEST(72 == deq.at(3));
    }
    {
        value_type v{42, 54, 69, 72};
        const std::list<int> lst = toml::get<std::list<int>>(std::move(v));
        std::list<int>::const_iterator iter = lst.begin();
        BOOST_TEST(42 == *(iter++));
        BOOST_TEST(54 == *(iter++));
        BOOST_TEST(69 == *(iter++));
        BOOST_TEST(72 == *(iter++));
    }
    {
        value_type v{42, 54, 69, 72};
        std::array<int, 4> ary = toml::get<std::array<int, 4>>(std::move(v));
        BOOST_TEST(42 == ary.at(0));
        BOOST_TEST(54 == ary.at(1));
        BOOST_TEST(69 == ary.at(2));
        BOOST_TEST(72 == ary.at(3));
    }
    {
        value_type v{42, 54, 69, 72};
        std::tuple<int, short, unsigned, long> tpl =
            toml::get<std::tuple<int, short, unsigned, long>>(std::move(v));
        BOOST_TEST(                      42  == std::get<0>(tpl));
        BOOST_TEST(static_cast<short   >(54) == std::get<1>(tpl));
        BOOST_TEST(static_cast<unsigned>(69) == std::get<2>(tpl));
        BOOST_TEST(static_cast<long    >(72) == std::get<3>(tpl));
    }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_toml_array_of_array, value_type, test_value_types)
{
    {
        const value_type v1{42, 54, 69, 72};
        const value_type v2{"foo", "bar", "baz"};
        const value_type v{v1, v2};

        std::pair<std::vector<int>, std::vector<std::string>> p =
            toml::get<std::pair<std::vector<int>, std::vector<std::string>>>(v);

        BOOST_TEST(p.first.size() == 4u);
        BOOST_TEST(p.first.at(0) == 42);
        BOOST_TEST(p.first.at(1) == 54);
        BOOST_TEST(p.first.at(2) == 69);
        BOOST_TEST(p.first.at(3) == 72);

        BOOST_TEST(p.second.size() == 3u);
        BOOST_TEST(p.second.at(0) == "foo");
        BOOST_TEST(p.second.at(1) == "bar");
        BOOST_TEST(p.second.at(2) == "baz");

        std::tuple<std::vector<int>, std::vector<std::string>> t =
            toml::get<std::tuple<std::vector<int>, std::vector<std::string>>>(v);

        BOOST_TEST(std::get<0>(t).at(0) == 42);
        BOOST_TEST(std::get<0>(t).at(1) == 54);
        BOOST_TEST(std::get<0>(t).at(2) == 69);
        BOOST_TEST(std::get<0>(t).at(3) == 72);

        BOOST_TEST(std::get<1>(t).at(0) == "foo");
        BOOST_TEST(std::get<1>(t).at(1) == "bar");
        BOOST_TEST(std::get<1>(t).at(2) == "baz");
    }
    {
        const value_type v1{42, 54, 69, 72};
        const value_type v2{"foo", "bar", "baz"};
        value_type v{v1, v2};

        std::pair<std::vector<int>, std::vector<std::string>> p =
            toml::get<std::pair<std::vector<int>, std::vector<std::string>>>(std::move(v));

        BOOST_TEST(p.first.size() == 4u);
        BOOST_TEST(p.first.at(0) == 42);
        BOOST_TEST(p.first.at(1) == 54);
        BOOST_TEST(p.first.at(2) == 69);
        BOOST_TEST(p.first.at(3) == 72);

        BOOST_TEST(p.second.size() == 3u);
        BOOST_TEST(p.second.at(0) == "foo");
        BOOST_TEST(p.second.at(1) == "bar");
        BOOST_TEST(p.second.at(2) == "baz");
    }
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_toml_table, value_type, test_value_types)
{
    {
        const value_type v1{
                {"key1", 1},
                {"key2", 2},
                {"key3", 3},
                {"key4", 4}
            };

        const auto v = toml::get<std::map<std::string, int>>(v1);
        BOOST_TEST(v.at("key1") == 1);
        BOOST_TEST(v.at("key2") == 2);
        BOOST_TEST(v.at("key3") == 3);
        BOOST_TEST(v.at("key4") == 4);
    }
    {
        value_type v1{
                {"key1", 1},
                {"key2", 2},
                {"key3", 3},
                {"key4", 4}
            };
        const auto v = toml::get<std::map<std::string, int>>(std::move(v1));
        BOOST_TEST(v.at("key1") == 1);
        BOOST_TEST(v.at("key2") == 2);
        BOOST_TEST(v.at("key3") == 3);
        BOOST_TEST(v.at("key4") == 4);
    }

}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_toml_local_date, value_type, test_value_types)
{
    value_type v1(toml::local_date{2018, toml::month_t::Apr, 1});
    const auto date = std::chrono::system_clock::to_time_t(
            toml::get<std::chrono::system_clock::time_point>(v1));

    std::tm t;
    t.tm_year = 2018 - 1900;
    t.tm_mon  = 4    - 1;
    t.tm_mday = 1;
    t.tm_hour = 0;
    t.tm_min  = 0;
    t.tm_sec  = 0;
    t.tm_isdst = -1;
    const auto c = std::mktime(&t);
    BOOST_TEST(c == date);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_toml_local_time, value_type, test_value_types)
{
    value_type v1(toml::local_time{12, 30, 45});
    const auto time = toml::get<std::chrono::seconds>(v1);
    const bool result = time == std::chrono::hours(12)   +
                                std::chrono::minutes(30) +
                                std::chrono::seconds(45);
    BOOST_TEST(result);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_toml_local_datetime, value_type, test_value_types)
{
    value_type v1(toml::local_datetime(
                toml::local_date{2018, toml::month_t::Apr, 1},
                toml::local_time{12, 30, 45}));

    const auto date = std::chrono::system_clock::to_time_t(
            toml::get<std::chrono::system_clock::time_point>(v1));
    std::tm t;
    t.tm_year = 2018 - 1900;
    t.tm_mon  = 4    - 1;
    t.tm_mday = 1;
    t.tm_hour = 12;
    t.tm_min  = 30;
    t.tm_sec  = 45;
    t.tm_isdst = -1;
    const auto c = std::mktime(&t);
    BOOST_TEST(c == date);
}

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_toml_offset_datetime, value_type, test_value_types)
{
    {
    value_type v1(toml::offset_datetime(
                toml::local_date{2018, toml::month_t::Apr, 1},
                toml::local_time{12, 30, 0},
                toml::time_offset{9, 0}));
    //    2018-04-01T12:30:00+09:00
    // == 2018-04-01T03:30:00Z

    const auto date = toml::get<std::chrono::system_clock::time_point>(v1);
    const auto timet = std::chrono::system_clock::to_time_t(date);

    // get time_t as gmtime (2018-04-01T03:30:00Z)
    const auto tmp = std::gmtime(std::addressof(timet)); // XXX not threadsafe!
    BOOST_TEST(tmp);
    const auto tm = *tmp;
    BOOST_TEST(tm.tm_year + 1900 == 2018);
    BOOST_TEST(tm.tm_mon  + 1 ==       4);
    BOOST_TEST(tm.tm_mday ==           1);
    BOOST_TEST(tm.tm_hour ==           3);
    BOOST_TEST(tm.tm_min ==           30);
    BOOST_TEST(tm.tm_sec ==            0);
    }

    {
    value_type v1(toml::offset_datetime(
                toml::local_date{2018, toml::month_t::Apr, 1},
                toml::local_time{12, 30, 0},
                toml::time_offset{-8, 0}));
    //    2018-04-01T12:30:00-08:00
    // == 2018-04-01T20:30:00Z

    const auto date = toml::get<std::chrono::system_clock::time_point>(v1);
    const auto timet = std::chrono::system_clock::to_time_t(date);

    // get time_t as gmtime (2018-04-01T03:30:00Z)
    const auto tmp = std::gmtime(std::addressof(timet)); // XXX not threadsafe!
    BOOST_TEST(tmp);
    const auto tm = *tmp;
    BOOST_TEST(tm.tm_year + 1900 == 2018);
    BOOST_TEST(tm.tm_mon  + 1 ==       4);
    BOOST_TEST(tm.tm_mday ==           1);
    BOOST_TEST(tm.tm_hour ==          20);
    BOOST_TEST(tm.tm_min ==           30);
    BOOST_TEST(tm.tm_sec ==            0);
    }
}
