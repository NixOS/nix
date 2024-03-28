#include <toml.hpp>

#include "unit_test.hpp"

#include <map>
#include <list>

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
#include <string_view>
#endif


BOOST_AUTO_TEST_CASE(test_value_boolean)
{
    toml::value v1(true);
    toml::value v2(false);

    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v2.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v2.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v2.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v2.is_boolean());

    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v2.cast<toml::value_t::boolean>() == false);
    BOOST_TEST(v1.as_boolean() == true);
    BOOST_TEST(v2.as_boolean() == false);
    BOOST_TEST(v1.as_boolean(std::nothrow) == true);
    BOOST_TEST(v2.as_boolean(std::nothrow) == false);

    v1 = false;
    v2 = true;

    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v2.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v2.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v2.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v2.is_boolean());

    BOOST_TEST(v1.cast<toml::value_t::boolean>() == false);
    BOOST_TEST(v2.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == false);
    BOOST_TEST(v2.as_boolean() == true);

    toml::value v3(v1);
    toml::value v4(v2);
    BOOST_TEST(v3 == v1);
    BOOST_TEST(v4 == v2);

    BOOST_TEST(v3.type() == toml::value_t::boolean);
    BOOST_TEST(v4.type() == toml::value_t::boolean);
    BOOST_TEST(v3.is(toml::value_t::boolean));
    BOOST_TEST(v4.is(toml::value_t::boolean));
    BOOST_TEST(v3.is<toml::boolean>());
    BOOST_TEST(v4.is<toml::boolean>());
    BOOST_TEST(v3.is_boolean());
    BOOST_TEST(v4.is_boolean());

    BOOST_TEST(v3.cast<toml::value_t::boolean>() == false);
    BOOST_TEST(v4.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v3.as_boolean() == false);
    BOOST_TEST(v4.as_boolean() == true);

    toml::value v5(std::move(v1));
    toml::value v6(std::move(v2));

    BOOST_TEST(v5.type() == toml::value_t::boolean);
    BOOST_TEST(v6.type() == toml::value_t::boolean);
    BOOST_TEST(v5.is(toml::value_t::boolean));
    BOOST_TEST(v6.is(toml::value_t::boolean));
    BOOST_TEST(v5.is<toml::boolean>());
    BOOST_TEST(v6.is<toml::boolean>());
    BOOST_TEST(v3.is_boolean());
    BOOST_TEST(v4.is_boolean());

    BOOST_TEST(v5.cast<toml::value_t::boolean>() == false);
    BOOST_TEST(v6.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v5.as_boolean() == false);
    BOOST_TEST(v6.as_boolean() == true);

    v1 = 42;
    v2 = 3.14;

    BOOST_TEST(v1.type() == toml::value_t::integer);
    BOOST_TEST(v2.type() == toml::value_t::floating);
    BOOST_TEST(v1.is(toml::value_t::integer));
    BOOST_TEST(v2.is(toml::value_t::floating));
    BOOST_TEST(v1.is<toml::integer>());
    BOOST_TEST(v2.is<toml::floating>());
    BOOST_TEST(v1.is_integer());
    BOOST_TEST(v2.is_floating());

    BOOST_TEST(v1.cast<toml::value_t::integer>() == 42);
    BOOST_TEST(v2.cast<toml::value_t::floating>() ==   3.14);
    BOOST_TEST(v1.as_integer() ==  42);
    BOOST_TEST(v2.as_floating() == 3.14);
}

BOOST_AUTO_TEST_CASE(test_value_integer)
{
    toml::value v1(-42);
    toml::value v2(42u);

    BOOST_TEST(v1.type() == toml::value_t::integer);
    BOOST_TEST(v2.type() == toml::value_t::integer);
    BOOST_TEST(v1.is(toml::value_t::integer));
    BOOST_TEST(v2.is(toml::value_t::integer));
    BOOST_TEST(v1.is<toml::integer>());
    BOOST_TEST(v2.is<toml::integer>());
    BOOST_TEST(v1.is_integer());
    BOOST_TEST(v2.is_integer());

    BOOST_TEST(v1.cast<toml::value_t::integer>() == -42);
    BOOST_TEST(v2.cast<toml::value_t::integer>() ==  42u);
    BOOST_TEST(v1.as_integer() == -42);
    BOOST_TEST(v2.as_integer() ==  42u);
    BOOST_TEST(v1.as_integer(std::nothrow) == -42);
    BOOST_TEST(v2.as_integer(std::nothrow) ==  42u);

    v1 = 54;
    v2 = -54;

    BOOST_TEST(v1.type() == toml::value_t::integer);
    BOOST_TEST(v2.type() == toml::value_t::integer);
    BOOST_TEST(v1.is(toml::value_t::integer));
    BOOST_TEST(v2.is(toml::value_t::integer));
    BOOST_TEST(v1.is<toml::integer>());
    BOOST_TEST(v2.is<toml::integer>());
    BOOST_TEST(v1.is_integer());
    BOOST_TEST(v2.is_integer());

    BOOST_TEST(v1.cast<toml::value_t::integer>() ==  54);
    BOOST_TEST(v2.cast<toml::value_t::integer>() == -54);
    BOOST_TEST(v1.as_integer() ==  54);
    BOOST_TEST(v2.as_integer() == -54);

    toml::value v3(v1);
    toml::value v4(v2);
    BOOST_TEST(v3 == v1);
    BOOST_TEST(v4 == v2);

    BOOST_TEST(v3.type() == toml::value_t::integer);
    BOOST_TEST(v4.type() == toml::value_t::integer);
    BOOST_TEST(v3.is(toml::value_t::integer));
    BOOST_TEST(v4.is(toml::value_t::integer));
    BOOST_TEST(v3.is<toml::integer>());
    BOOST_TEST(v4.is<toml::integer>());
    BOOST_TEST(v3.is_integer());
    BOOST_TEST(v4.is_integer());

    BOOST_TEST(v3.cast<toml::value_t::integer>() ==  54);
    BOOST_TEST(v4.cast<toml::value_t::integer>() == -54);
    BOOST_TEST(v3.as_integer() ==  54);
    BOOST_TEST(v4.as_integer() == -54);

    toml::value v5(std::move(v1));
    toml::value v6(std::move(v2));

    BOOST_TEST(v5.type() == toml::value_t::integer);
    BOOST_TEST(v6.type() == toml::value_t::integer);
    BOOST_TEST(v5.is(toml::value_t::integer));
    BOOST_TEST(v6.is(toml::value_t::integer));
    BOOST_TEST(v5.is<toml::integer>());
    BOOST_TEST(v6.is<toml::integer>());
    BOOST_TEST(v5.is_integer());
    BOOST_TEST(v6.is_integer());

    BOOST_TEST(v5.cast<toml::value_t::integer>() ==  54);
    BOOST_TEST(v6.cast<toml::value_t::integer>() == -54);
    BOOST_TEST(v5.as_integer() ==  54);
    BOOST_TEST(v6.as_integer() == -54);

    v1 = true;
    v2 = false;

    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v2.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v2.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v2.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v2.is_boolean());

    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v2.cast<toml::value_t::boolean>() == false);
    BOOST_TEST(v1.as_boolean() == true);
    BOOST_TEST(v2.as_boolean() == false);
}

BOOST_AUTO_TEST_CASE(test_value_float)
{
    toml::value v1(3.14);
    toml::value v2(3.14f);

    BOOST_TEST(v1.type() == toml::value_t::floating);
    BOOST_TEST(v2.type() == toml::value_t::floating);
    BOOST_TEST(v1.is(toml::value_t::floating));
    BOOST_TEST(v2.is(toml::value_t::floating));
    BOOST_TEST(v1.is<toml::floating>());
    BOOST_TEST(v2.is<toml::floating>());
    BOOST_TEST(v1.is_floating());
    BOOST_TEST(v2.is_floating());

    BOOST_TEST(v1.cast<toml::value_t::floating>() == 3.14);
    BOOST_TEST(v2.cast<toml::value_t::floating>() == 3.14,
               boost::test_tools::tolerance(1e-2));
    BOOST_TEST(v1.as_floating() == 3.14);
    BOOST_TEST(v2.as_floating() == 3.14,
               boost::test_tools::tolerance(1e-2));
    BOOST_TEST(v1.as_floating(std::nothrow) == 3.14);
    BOOST_TEST(v2.as_floating(std::nothrow) == 3.14,
               boost::test_tools::tolerance(1e-2));

    v1 = 2.718f;
    v2 = 2.718;

    BOOST_TEST(v1.type() == toml::value_t::floating);
    BOOST_TEST(v2.type() == toml::value_t::floating);
    BOOST_TEST(v1.is(toml::value_t::floating));
    BOOST_TEST(v2.is(toml::value_t::floating));
    BOOST_TEST(v1.is<toml::floating>());
    BOOST_TEST(v2.is<toml::floating>());
    BOOST_TEST(v1.is_floating());
    BOOST_TEST(v2.is_floating());

    BOOST_TEST(v1.cast<toml::value_t::floating>() == 2.718,
               boost::test_tools::tolerance(1e-3));
    BOOST_TEST(v2.cast<toml::value_t::floating>() == 2.718);
    BOOST_TEST(v1.as_floating() == 2.718,
               boost::test_tools::tolerance(1e-3));
    BOOST_TEST(v2.as_floating() == 2.718);

    toml::value v3(v1);
    toml::value v4(v2);
    BOOST_TEST(v3 == v1);
    BOOST_TEST(v4 == v2);

    BOOST_TEST(v3.type() == toml::value_t::floating);
    BOOST_TEST(v4.type() == toml::value_t::floating);
    BOOST_TEST(v3.is(toml::value_t::floating));
    BOOST_TEST(v4.is(toml::value_t::floating));
    BOOST_TEST(v3.is<toml::floating>());
    BOOST_TEST(v4.is<toml::floating>());
    BOOST_TEST(v3.is_floating());
    BOOST_TEST(v4.is_floating());

    BOOST_TEST(v3.cast<toml::value_t::floating>() == 2.718,
               boost::test_tools::tolerance(1e-3));
    BOOST_TEST(v4.cast<toml::value_t::floating>() == 2.718);
    BOOST_TEST(v3.as_floating() == 2.718,
               boost::test_tools::tolerance(1e-3));
    BOOST_TEST(v4.as_floating() == 2.718);

    toml::value v5(std::move(v1));
    toml::value v6(std::move(v2));

    BOOST_TEST(v5.type() == toml::value_t::floating);
    BOOST_TEST(v6.type() == toml::value_t::floating);
    BOOST_TEST(v5.is(toml::value_t::floating));
    BOOST_TEST(v6.is(toml::value_t::floating));
    BOOST_TEST(v5.is<toml::floating>());
    BOOST_TEST(v6.is<toml::floating>());
    BOOST_TEST(v5.is_floating());
    BOOST_TEST(v6.is_floating());

    BOOST_TEST(v5.cast<toml::value_t::floating>() == 2.718,
               boost::test_tools::tolerance(1e-3));
    BOOST_TEST(v6.cast<toml::value_t::floating>() == 2.718);
    BOOST_TEST(v5.as_floating() == 2.718,
               boost::test_tools::tolerance(1e-3));
    BOOST_TEST(v6.as_floating() == 2.718);

    v1 = true;
    v2 = false;

    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v2.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v2.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v2.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v2.is_boolean());

    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v2.cast<toml::value_t::boolean>() == false);
    BOOST_TEST(v1.as_boolean() == true);
    BOOST_TEST(v2.as_boolean() == false);
}

BOOST_AUTO_TEST_CASE(test_value_string)
{
    toml::value v1(std::string("foo"));
    toml::value v2(std::string("foo"), toml::string_t::literal);
    toml::value v3("foo");

    BOOST_TEST(v1.type() == toml::value_t::string);
    BOOST_TEST(v2.type() == toml::value_t::string);
    BOOST_TEST(v3.type() == toml::value_t::string);
    BOOST_TEST(v1.is(toml::value_t::string));
    BOOST_TEST(v2.is(toml::value_t::string));
    BOOST_TEST(v3.is(toml::value_t::string));
    BOOST_TEST(v1.is<toml::string>());
    BOOST_TEST(v2.is<toml::string>());
    BOOST_TEST(v3.is<toml::string>());
    BOOST_TEST(v1.is_string());
    BOOST_TEST(v2.is_string());
    BOOST_TEST(v3.is_string());

    BOOST_TEST(v1.cast<toml::value_t::string>() == "foo");
    BOOST_TEST(v2.cast<toml::value_t::string>() == "foo");
    BOOST_TEST(v3.cast<toml::value_t::string>() == "foo");
    BOOST_TEST(v1.as_string() == "foo");
    BOOST_TEST(v2.as_string() == "foo");
    BOOST_TEST(v3.as_string() == "foo");
    BOOST_TEST(v1.as_string(std::nothrow) == "foo");
    BOOST_TEST(v2.as_string(std::nothrow) == "foo");
    BOOST_TEST(v3.as_string(std::nothrow) == "foo");

    v1 = "bar";
    v2 = "bar";
    v3 = "bar";

    BOOST_TEST(v1.type() == toml::value_t::string);
    BOOST_TEST(v2.type() == toml::value_t::string);
    BOOST_TEST(v3.type() == toml::value_t::string);
    BOOST_TEST(v1.is(toml::value_t::string));
    BOOST_TEST(v2.is(toml::value_t::string));
    BOOST_TEST(v3.is(toml::value_t::string));
    BOOST_TEST(v1.is_string());
    BOOST_TEST(v2.is_string());
    BOOST_TEST(v3.is_string());

    BOOST_TEST(v1.cast<toml::value_t::string>() == "bar");
    BOOST_TEST(v2.cast<toml::value_t::string>() == "bar");
    BOOST_TEST(v3.cast<toml::value_t::string>() == "bar");
    BOOST_TEST(v1.as_string() == "bar");
    BOOST_TEST(v2.as_string() == "bar");
    BOOST_TEST(v3.as_string() == "bar");


    toml::value v4(v1);
    toml::value v5(v2);
    toml::value v6(v3);
    BOOST_TEST(v4 == v1);
    BOOST_TEST(v5 == v2);
    BOOST_TEST(v6 == v3);

    BOOST_TEST(v4.type() == toml::value_t::string);
    BOOST_TEST(v5.type() == toml::value_t::string);
    BOOST_TEST(v6.type() == toml::value_t::string);
    BOOST_TEST(v4.is(toml::value_t::string));
    BOOST_TEST(v5.is(toml::value_t::string));
    BOOST_TEST(v6.is(toml::value_t::string));
    BOOST_TEST(v4.is<toml::string>());
    BOOST_TEST(v5.is<toml::string>());
    BOOST_TEST(v6.is<toml::string>());
    BOOST_TEST(v4.is_string());
    BOOST_TEST(v5.is_string());
    BOOST_TEST(v6.is_string());

    BOOST_TEST(v4.cast<toml::value_t::string>() == "bar");
    BOOST_TEST(v5.cast<toml::value_t::string>() == "bar");
    BOOST_TEST(v6.cast<toml::value_t::string>() == "bar");
    BOOST_TEST(v4.as_string() == "bar");
    BOOST_TEST(v5.as_string() == "bar");
    BOOST_TEST(v6.as_string() == "bar");


    v4.cast<toml::value_t::string>().str.at(2) = 'z';
    v5.cast<toml::value_t::string>().str.at(2) = 'z';
    v6.cast<toml::value_t::string>().str.at(2) = 'z';

    BOOST_TEST(v4.type() == toml::value_t::string);
    BOOST_TEST(v5.type() == toml::value_t::string);
    BOOST_TEST(v6.type() == toml::value_t::string);
    BOOST_TEST(v4.is(toml::value_t::string));
    BOOST_TEST(v5.is(toml::value_t::string));
    BOOST_TEST(v6.is(toml::value_t::string));
    BOOST_TEST(v4.is<toml::string>());
    BOOST_TEST(v5.is<toml::string>());
    BOOST_TEST(v6.is<toml::string>());
    BOOST_TEST(v4.is_string());
    BOOST_TEST(v5.is_string());
    BOOST_TEST(v6.is_string());

    BOOST_TEST(v4.as_string() == "baz");
    BOOST_TEST(v5.as_string() == "baz");
    BOOST_TEST(v6.as_string() == "baz");

    v1 = true;
    v2 = true;
    v3 = true;

    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v2.type() == toml::value_t::boolean);
    BOOST_TEST(v3.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v2.is(toml::value_t::boolean));
    BOOST_TEST(v3.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v2.is<toml::boolean>());
    BOOST_TEST(v3.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v2.is_boolean());
    BOOST_TEST(v3.is_boolean());

    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v2.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v3.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == true);
    BOOST_TEST(v2.as_boolean() == true);
    BOOST_TEST(v3.as_boolean() == true);

#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
    std::string_view sv = "foo";

    toml::value v7(sv);
    toml::value v8(sv, toml::string_t::literal);

    BOOST_TEST(v7.type() == toml::value_t::string);
    BOOST_TEST(v8.type() == toml::value_t::string);
    BOOST_TEST(v7.is(toml::value_t::string));
    BOOST_TEST(v8.is(toml::value_t::string));
    BOOST_TEST(v7.is<toml::string>());
    BOOST_TEST(v8.is<toml::string>());
    BOOST_TEST(v7.is_string());
    BOOST_TEST(v8.is_string());

    BOOST_TEST(v7.cast<toml::value_t::string>() == "foo");
    BOOST_TEST(v8.cast<toml::value_t::string>() == "foo");
#endif
}

BOOST_AUTO_TEST_CASE(test_value_local_date)
{
    toml::value v1(toml::local_date(2018, toml::month_t::Jan, 31));

    BOOST_TEST(v1.type() == toml::value_t::local_date);
    BOOST_TEST(v1.is(toml::value_t::local_date));
    BOOST_TEST(v1.is<toml::local_date>());
    BOOST_TEST(v1.is_local_date());

    BOOST_TEST(v1.cast<toml::value_t::local_date>() ==
                      toml::local_date(2018, toml::month_t::Jan, 31));
    BOOST_TEST(v1.as_local_date() ==
                      toml::local_date(2018, toml::month_t::Jan, 31));
    BOOST_TEST(v1.as_local_date(std::nothrow) ==
                      toml::local_date(2018, toml::month_t::Jan, 31));

    v1 = toml::local_date(2018, toml::month_t::Apr, 1);

    BOOST_TEST(v1.type() == toml::value_t::local_date);
    BOOST_TEST(v1.is(toml::value_t::local_date));
    BOOST_TEST(v1.is<toml::local_date>());
    BOOST_TEST(v1.is_local_date());

    BOOST_TEST(v1.cast<toml::value_t::local_date>() ==
                      toml::local_date(2018, toml::month_t::Apr, 1));
    BOOST_TEST(v1.as_local_date() ==
                      toml::local_date(2018, toml::month_t::Apr, 1));

    toml::value v2(v1);
    BOOST_TEST(v2 == v1);

    BOOST_TEST(v2.type() == toml::value_t::local_date);
    BOOST_TEST(v2.is(toml::value_t::local_date));
    BOOST_TEST(v2.is<toml::local_date>());
    BOOST_TEST(v2.is_local_date());

    BOOST_TEST(v2.cast<toml::value_t::local_date>() ==
                      toml::local_date(2018, toml::month_t::Apr, 1));
    BOOST_TEST(v2.as_local_date() ==
                      toml::local_date(2018, toml::month_t::Apr, 1));

    v1 = true;
    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == true);
}

BOOST_AUTO_TEST_CASE(test_value_local_time)
{
    toml::value v1(toml::local_time(12, 30, 45));
    toml::value v2(std::chrono::hours(12) + std::chrono::minutes(30) +
                   std::chrono::seconds(45));

    BOOST_TEST(v1.type() == toml::value_t::local_time);
    BOOST_TEST(v2.type() == toml::value_t::local_time);
    BOOST_TEST(v1.is(toml::value_t::local_time));
    BOOST_TEST(v2.is(toml::value_t::local_time));
    BOOST_TEST(v1.is<toml::local_time>());
    BOOST_TEST(v2.is<toml::local_time>());
    BOOST_TEST(v1.is_local_time());
    BOOST_TEST(v2.is_local_time());

    BOOST_TEST(v1.cast<toml::value_t::local_time>() ==
                      toml::local_time(12, 30, 45));
    BOOST_TEST(v1.as_local_time() ==
                      toml::local_time(12, 30, 45));

    BOOST_TEST(v2.cast<toml::value_t::local_time>() ==
                      toml::local_time(12, 30, 45));
    BOOST_TEST(v2.as_local_time() ==
                      toml::local_time(12, 30, 45));

    BOOST_TEST(v1.cast<toml::value_t::local_time>() ==
                      v2.cast<toml::value_t::local_time>());
    BOOST_TEST(v1.as_local_time() ==
                      v2.as_local_time());
    BOOST_TEST(v1.as_local_time(std::nothrow) ==
                      v2.as_local_time(std::nothrow));

    v1 = toml::local_time(1, 30, 0, /*ms*/ 100, /*us*/ 0);

    BOOST_TEST(v1.type() == toml::value_t::local_time);
    BOOST_TEST(v1.is(toml::value_t::local_time));
    BOOST_TEST(v1.is<toml::local_time>());
    BOOST_TEST(v1.is_local_time());
    BOOST_TEST(v1.cast<toml::value_t::local_time>() ==
                      toml::local_time(1, 30, 0, 100, 0));
    BOOST_TEST(v1.as_local_time() ==
                      toml::local_time(1, 30, 0, 100, 0));

    toml::value v3(v1);
    BOOST_TEST(v3 == v1);

    BOOST_TEST(v3.type() == toml::value_t::local_time);
    BOOST_TEST(v3.is(toml::value_t::local_time));
    BOOST_TEST(v3.is<toml::local_time>());
    BOOST_TEST(v3.is_local_time());

    BOOST_TEST(v3.cast<toml::value_t::local_time>() ==
                      toml::local_time(1, 30, 0, 100, 0));
    BOOST_TEST(v3.as_local_time() ==
                      toml::local_time(1, 30, 0, 100, 0));

    v1 = true;
    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == true);
}

BOOST_AUTO_TEST_CASE(test_value_local_datetime)
{
    toml::value v1(toml::local_datetime(
                toml::local_date(2018, toml::month_t::Jan, 31),
                toml::local_time(12, 30, 45)
                ));

    BOOST_TEST(v1.type() == toml::value_t::local_datetime);
    BOOST_TEST(v1.is(toml::value_t::local_datetime));
    BOOST_TEST(v1.is<toml::local_datetime>());
    BOOST_TEST(v1.is_local_datetime());

    BOOST_TEST(v1.cast<toml::value_t::local_datetime>() ==
                      toml::local_datetime(
                          toml::local_date(2018, toml::month_t::Jan, 31),
                          toml::local_time(12, 30, 45)));
    BOOST_TEST(v1.as_local_datetime() ==
                      toml::local_datetime(
                          toml::local_date(2018, toml::month_t::Jan, 31),
                          toml::local_time(12, 30, 45)));
    BOOST_TEST(v1.as_local_datetime(std::nothrow) ==
                      toml::local_datetime(
                          toml::local_date(2018, toml::month_t::Jan, 31),
                          toml::local_time(12, 30, 45)));


    v1 = toml::local_datetime(
                toml::local_date(2018, toml::month_t::Apr, 1),
                toml::local_time(1, 15, 30));

    BOOST_TEST(v1.type() == toml::value_t::local_datetime);
    BOOST_TEST(v1.is(toml::value_t::local_datetime));
    BOOST_TEST(v1.is<toml::local_datetime>());
    BOOST_TEST(v1.is_local_datetime());

    BOOST_TEST(v1.cast<toml::value_t::local_datetime>() ==
                      toml::local_datetime(
                          toml::local_date(2018, toml::month_t::Apr, 1),
                          toml::local_time(1, 15, 30)));
    BOOST_TEST(v1.as_local_datetime() ==
                      toml::local_datetime(
                          toml::local_date(2018, toml::month_t::Apr, 1),
                          toml::local_time(1, 15, 30)));

    toml::value v2(v1);
    BOOST_TEST(v2 == v1);

    BOOST_TEST(v2.type() == toml::value_t::local_datetime);
    BOOST_TEST(v2.is(toml::value_t::local_datetime));
    BOOST_TEST(v2.is<toml::local_datetime>());
    BOOST_TEST(v2.is_local_datetime());

    BOOST_TEST(v2.cast<toml::value_t::local_datetime>() ==
                      toml::local_datetime(
                          toml::local_date(2018, toml::month_t::Apr, 1),
                          toml::local_time(1, 15, 30)));
    BOOST_TEST(v2.as_local_datetime() ==
                      toml::local_datetime(
                          toml::local_date(2018, toml::month_t::Apr, 1),
                          toml::local_time(1, 15, 30)));


    v1 = true;
    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == true);
}

BOOST_AUTO_TEST_CASE(test_value_offset_datetime)
{
    toml::value v1(toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Jan, 31),
                toml::local_time(12, 30, 45),
                toml::time_offset(9, 0)
                ));

    BOOST_TEST(v1.type() == toml::value_t::offset_datetime);
    BOOST_TEST(v1.is(toml::value_t::offset_datetime));
    BOOST_TEST(v1.is<toml::offset_datetime>());
    BOOST_TEST(v1.is_offset_datetime());

    BOOST_TEST(v1.cast<toml::value_t::offset_datetime>() ==
            toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Jan, 31),
                toml::local_time(12, 30, 45),
                toml::time_offset(9, 0)
                ));
    BOOST_TEST(v1.as_offset_datetime() ==
            toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Jan, 31),
                toml::local_time(12, 30, 45),
                toml::time_offset(9, 0)
                ));
    BOOST_TEST(v1.as_offset_datetime(std::nothrow) ==
            toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Jan, 31),
                toml::local_time(12, 30, 45),
                toml::time_offset(9, 0)
                ));


    v1 = toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Apr, 1),
                toml::local_time(1, 15, 30),
                toml::time_offset(9, 0));

    BOOST_TEST(v1.type() == toml::value_t::offset_datetime);
    BOOST_TEST(v1.is(toml::value_t::offset_datetime));
    BOOST_TEST(v1.is<toml::offset_datetime>());
    BOOST_TEST(v1.is_offset_datetime());

    BOOST_TEST(v1.cast<toml::value_t::offset_datetime>() ==
            toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Apr, 1),
                toml::local_time(1, 15, 30),
                toml::time_offset(9, 0)));
    BOOST_TEST(v1.as_offset_datetime() ==
            toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Apr, 1),
                toml::local_time(1, 15, 30),
                toml::time_offset(9, 0)));


    toml::value v2(v1);
    BOOST_TEST(v2 == v1);

    BOOST_TEST(v2.type() == toml::value_t::offset_datetime);
    BOOST_TEST(v2.is(toml::value_t::offset_datetime));
    BOOST_TEST(v2.is<toml::offset_datetime>());
    BOOST_TEST(v2.is_offset_datetime());

    BOOST_TEST(v2.cast<toml::value_t::offset_datetime>() ==
            toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Apr, 1),
                toml::local_time(1, 15, 30),
                toml::time_offset(9, 0)));
    BOOST_TEST(v2.as_offset_datetime() ==
            toml::offset_datetime(
                toml::local_date(2018, toml::month_t::Apr, 1),
                toml::local_time(1, 15, 30),
                toml::time_offset(9, 0)));

    v1 = true;
    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == true);
}

BOOST_AUTO_TEST_CASE(test_value_array)
{
    std::vector<int> v{1,2,3,4,5};
    toml::value v1(v);
    toml::value v2{6,7,8,9,0};

    BOOST_TEST(v1.type() == toml::value_t::array);
    BOOST_TEST(v1.is(toml::value_t::array));
    BOOST_TEST(v1.is<toml::array>());
    BOOST_TEST(v1.is_array());

    BOOST_TEST(v2.type() == toml::value_t::array);
    BOOST_TEST(v2.is(toml::value_t::array));
    BOOST_TEST(v2.is<toml::array>());
    BOOST_TEST(v2.is_array());

    BOOST_TEST(v1.cast<toml::value_t::array>().at(0).cast<toml::value_t::integer>() == 1);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(1).cast<toml::value_t::integer>() == 2);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(2).cast<toml::value_t::integer>() == 3);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(3).cast<toml::value_t::integer>() == 4);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(4).cast<toml::value_t::integer>() == 5);
    BOOST_TEST(v1.as_array().at(0).as_integer() == 1);
    BOOST_TEST(v1.as_array().at(1).as_integer() == 2);
    BOOST_TEST(v1.as_array().at(2).as_integer() == 3);
    BOOST_TEST(v1.as_array().at(3).as_integer() == 4);
    BOOST_TEST(v1.as_array().at(4).as_integer() == 5);
    BOOST_TEST(v1.as_array(std::nothrow).at(0).as_integer() == 1);
    BOOST_TEST(v1.as_array(std::nothrow).at(1).as_integer() == 2);
    BOOST_TEST(v1.as_array(std::nothrow).at(2).as_integer() == 3);
    BOOST_TEST(v1.as_array(std::nothrow).at(3).as_integer() == 4);
    BOOST_TEST(v1.as_array(std::nothrow).at(4).as_integer() == 5);

    BOOST_TEST(v2.cast<toml::value_t::array>().at(0).cast<toml::value_t::integer>() == 6);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(1).cast<toml::value_t::integer>() == 7);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(2).cast<toml::value_t::integer>() == 8);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(3).cast<toml::value_t::integer>() == 9);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(4).cast<toml::value_t::integer>() == 0);

    v1 = {6,7,8,9,0};
    v2 = v;

    BOOST_TEST(v1.type() == toml::value_t::array);
    BOOST_TEST(v1.is(toml::value_t::array));
    BOOST_TEST(v1.is<toml::array>());
    BOOST_TEST(v1.is_array());

    BOOST_TEST(v2.type() == toml::value_t::array);
    BOOST_TEST(v2.is(toml::value_t::array));
    BOOST_TEST(v2.is<toml::array>());
    BOOST_TEST(v2.is_array());

    BOOST_TEST(v1.cast<toml::value_t::array>().at(0).cast<toml::value_t::integer>() == 6);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(1).cast<toml::value_t::integer>() == 7);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(2).cast<toml::value_t::integer>() == 8);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(3).cast<toml::value_t::integer>() == 9);
    BOOST_TEST(v1.cast<toml::value_t::array>().at(4).cast<toml::value_t::integer>() == 0);
    BOOST_TEST(v1.as_array().at(0).as_integer() == 6);
    BOOST_TEST(v1.as_array().at(1).as_integer() == 7);
    BOOST_TEST(v1.as_array().at(2).as_integer() == 8);
    BOOST_TEST(v1.as_array().at(3).as_integer() == 9);
    BOOST_TEST(v1.as_array().at(4).as_integer() == 0);


    BOOST_TEST(v2.cast<toml::value_t::array>().at(0).cast<toml::value_t::integer>() == 1);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(1).cast<toml::value_t::integer>() == 2);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(2).cast<toml::value_t::integer>() == 3);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(3).cast<toml::value_t::integer>() == 4);
    BOOST_TEST(v2.cast<toml::value_t::array>().at(4).cast<toml::value_t::integer>() == 5);
    BOOST_TEST(v2.as_array().at(0).as_integer() == 1);
    BOOST_TEST(v2.as_array().at(1).as_integer() == 2);
    BOOST_TEST(v2.as_array().at(2).as_integer() == 3);
    BOOST_TEST(v2.as_array().at(3).as_integer() == 4);
    BOOST_TEST(v2.as_array().at(4).as_integer() == 5);


    toml::value v3(v1);
    BOOST_TEST(v3 == v1);

    BOOST_TEST(v3.type() == toml::value_t::array);
    BOOST_TEST(v3.is(toml::value_t::array));
    BOOST_TEST(v3.is<toml::array>());
    BOOST_TEST(v3.is_array());

    BOOST_TEST(v3.cast<toml::value_t::array>().at(0).cast<toml::value_t::integer>() == 6);
    BOOST_TEST(v3.cast<toml::value_t::array>().at(1).cast<toml::value_t::integer>() == 7);
    BOOST_TEST(v3.cast<toml::value_t::array>().at(2).cast<toml::value_t::integer>() == 8);
    BOOST_TEST(v3.cast<toml::value_t::array>().at(3).cast<toml::value_t::integer>() == 9);
    BOOST_TEST(v3.cast<toml::value_t::array>().at(4).cast<toml::value_t::integer>() == 0);
    BOOST_TEST(v3.as_array().at(0).as_integer() == 6);
    BOOST_TEST(v3.as_array().at(1).as_integer() == 7);
    BOOST_TEST(v3.as_array().at(2).as_integer() == 8);
    BOOST_TEST(v3.as_array().at(3).as_integer() == 9);
    BOOST_TEST(v3.as_array().at(4).as_integer() == 0);


    v1 = true;
    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == true);
}

BOOST_AUTO_TEST_CASE(test_value_table)
{
    toml::value v1{{"foo", 42}, {"bar", 3.14}, {"baz", "qux"}};

    BOOST_TEST(v1.type() == toml::value_t::table);
    BOOST_TEST(v1.is(toml::value_t::table));
    BOOST_TEST(v1.is<toml::table>());
    BOOST_TEST(v1.is_table());

    BOOST_TEST(v1.cast<toml::value_t::table>().at("foo").cast<toml::value_t::integer>() ==    42);
    BOOST_TEST(v1.cast<toml::value_t::table>().at("bar").cast<toml::value_t::floating>() ==   3.14);
    BOOST_TEST(v1.cast<toml::value_t::table>().at("baz").cast<toml::value_t::string>().str == "qux");
    BOOST_TEST(v1.as_table().at("foo").as_integer() ==    42);
    BOOST_TEST(v1.as_table().at("bar").as_floating() ==   3.14);
    BOOST_TEST(v1.as_table().at("baz").as_string().str == "qux");
    BOOST_TEST(v1.as_table(std::nothrow).at("foo").as_integer() ==    42);
    BOOST_TEST(v1.as_table(std::nothrow).at("bar").as_floating() ==   3.14);
    BOOST_TEST(v1.as_table(std::nothrow).at("baz").as_string().str == "qux");

    v1 = {{"foo", 2.71}, {"bar", 54}, {"baz", "quux"}};

    BOOST_TEST(v1.type() == toml::value_t::table);
    BOOST_TEST(v1.is(toml::value_t::table));
    BOOST_TEST(v1.is<toml::table>());
    BOOST_TEST(v1.is_table());

    BOOST_TEST(v1.cast<toml::value_t::table>().at("foo").cast<toml::value_t::floating>() ==      2.71);
    BOOST_TEST(v1.cast<toml::value_t::table>().at("bar").cast<toml::value_t::integer>() ==    54);
    BOOST_TEST(v1.cast<toml::value_t::table>().at("baz").cast<toml::value_t::string>().str == "quux");
    BOOST_TEST(v1.as_table().at("foo").as_floating() ==   2.71);
    BOOST_TEST(v1.as_table().at("bar").as_integer() ==    54);
    BOOST_TEST(v1.as_table().at("baz").as_string().str == "quux");

    v1 = toml::table{{"foo", 2.71}, {"bar", 54}, {"baz", "quux"}};

    BOOST_TEST(v1.type() == toml::value_t::table);
    BOOST_TEST(v1.is(toml::value_t::table));
    BOOST_TEST(v1.is<toml::table>());
    BOOST_TEST(v1.is_table());

    BOOST_TEST(v1.cast<toml::value_t::table>().at("foo").cast<toml::value_t::floating>() ==      2.71);
    BOOST_TEST(v1.cast<toml::value_t::table>().at("bar").cast<toml::value_t::integer>() ==    54);
    BOOST_TEST(v1.cast<toml::value_t::table>().at("baz").cast<toml::value_t::string>().str == "quux");
    BOOST_TEST(v1.as_table().at("foo").as_floating() ==   2.71);
    BOOST_TEST(v1.as_table().at("bar").as_integer() ==    54);
    BOOST_TEST(v1.as_table().at("baz").as_string().str == "quux");

    toml::value v3(v1);
    BOOST_TEST(v3 == v1);

    BOOST_TEST(v3.type() == toml::value_t::table);
    BOOST_TEST(v3.is(toml::value_t::table));
    BOOST_TEST(v3.is<toml::table>());
    BOOST_TEST(v3.is_table());

    BOOST_TEST(v3.cast<toml::value_t::table>().at("foo").cast<toml::value_t::floating>() ==   2.71);
    BOOST_TEST(v3.cast<toml::value_t::table>().at("bar").cast<toml::value_t::integer>() ==    54);
    BOOST_TEST(v3.cast<toml::value_t::table>().at("baz").cast<toml::value_t::string>().str == "quux");
    BOOST_TEST(v3.as_table().at("foo").as_floating() ==   2.71);
    BOOST_TEST(v3.as_table().at("bar").as_integer() ==    54);
    BOOST_TEST(v3.as_table().at("baz").as_string().str == "quux");


    v1 = true;
    BOOST_TEST(v1.type() == toml::value_t::boolean);
    BOOST_TEST(v1.is(toml::value_t::boolean));
    BOOST_TEST(v1.is<toml::boolean>());
    BOOST_TEST(v1.is_boolean());
    BOOST_TEST(v1.cast<toml::value_t::boolean>() == true);
    BOOST_TEST(v1.as_boolean() == true);
}

BOOST_AUTO_TEST_CASE(test_value_empty)
{
    toml::value v1;
    BOOST_TEST(v1.is_uninitialized());
    BOOST_TEST(v1.is(toml::value_t::empty));

    BOOST_CHECK_THROW(v1.as_boolean(),         toml::type_error);
    BOOST_CHECK_THROW(v1.as_integer(),         toml::type_error);
    BOOST_CHECK_THROW(v1.as_floating(),        toml::type_error);
    BOOST_CHECK_THROW(v1.as_string(),          toml::type_error);
    BOOST_CHECK_THROW(v1.as_offset_datetime(), toml::type_error);
    BOOST_CHECK_THROW(v1.as_local_datetime(),  toml::type_error);
    BOOST_CHECK_THROW(v1.as_local_date(),      toml::type_error);
    BOOST_CHECK_THROW(v1.as_local_time(),      toml::type_error);
    BOOST_CHECK_THROW(v1.as_array(),           toml::type_error);
    BOOST_CHECK_THROW(v1.as_table(),           toml::type_error);
}


BOOST_AUTO_TEST_CASE(test_value_at)
{
    {
        toml::value v1{{"foo", 42}, {"bar", 3.14}, {"baz", "qux"}};

        BOOST_TEST(v1.at("foo").as_integer()  == 42);
        BOOST_TEST(v1.at("bar").as_floating() == 3.14);
        BOOST_TEST(v1.at("baz").as_string()   == "qux");

        BOOST_CHECK_THROW(v1.at(0), toml::type_error);
        BOOST_CHECK_THROW(v1.at("quux"), std::out_of_range);
    }


    {
        toml::value v1{1,2,3,4,5};

        BOOST_TEST(v1.at(0).as_integer() == 1);
        BOOST_TEST(v1.at(1).as_integer() == 2);
        BOOST_TEST(v1.at(2).as_integer() == 3);
        BOOST_TEST(v1.at(3).as_integer() == 4);
        BOOST_TEST(v1.at(4).as_integer() == 5);

        BOOST_CHECK_THROW(v1.at("foo"), toml::type_error);
        BOOST_CHECK_THROW(v1.at(5),     std::out_of_range);
    }
}

BOOST_AUTO_TEST_CASE(test_value_bracket)
{
    {
        toml::value v1{{"foo", 42}, {"bar", 3.14}, {"baz", "qux"}};

        BOOST_TEST(v1["foo"].as_integer()  == 42);
        BOOST_TEST(v1["bar"].as_floating() == 3.14);
        BOOST_TEST(v1["baz"].as_string()   == "qux");

        v1["qux"] = 54;
        BOOST_TEST(v1["qux"].as_integer()  == 54);
    }
    {
        toml::value v1;
        v1["foo"] = 42;

        BOOST_TEST(v1.is_table());
        BOOST_TEST(v1["foo"].as_integer()  == 42);
    }
    {
        toml::value v1{1,2,3,4,5};

        BOOST_TEST(v1[0].as_integer() == 1);
        BOOST_TEST(v1[1].as_integer() == 2);
        BOOST_TEST(v1[2].as_integer() == 3);
        BOOST_TEST(v1[3].as_integer() == 4);
        BOOST_TEST(v1[4].as_integer() == 5);

        BOOST_CHECK_THROW(v1["foo"], toml::type_error);
    }
}

BOOST_AUTO_TEST_CASE(test_value_map_methods)
{
    {
        toml::value v1{{"foo", 42}, {"bar", 3.14}, {"baz", "qux"}};

        BOOST_TEST(v1.count("foo") == 1u);
        BOOST_TEST(v1.count("bar") == 1u);
        BOOST_TEST(v1.count("baz") == 1u);
        BOOST_TEST(v1.count("qux") == 0u);

        BOOST_TEST( v1.contains("foo"));
        BOOST_TEST( v1.contains("bar"));
        BOOST_TEST( v1.contains("baz"));
        BOOST_TEST(!v1.contains("qux"));

        BOOST_TEST(v1.size() == 3);

        v1["qux"] = 54;
        BOOST_TEST(v1.count("qux") == 1u);
        BOOST_TEST(v1.contains("qux"));
        BOOST_TEST(v1.size() == 4);
    }
    {
        toml::value v1(42);
        BOOST_CHECK_THROW(v1.size()       , toml::type_error);
        BOOST_CHECK_THROW(v1.count("k")   , toml::type_error);
        BOOST_CHECK_THROW(v1.contains("k"), toml::type_error);
    }
}

BOOST_AUTO_TEST_CASE(test_value_vector_methods)
{
    {
        toml::value v1{1, 2, 3, 4, 5};

        BOOST_TEST(v1.size() == 5);

        v1.push_back(6);
        BOOST_TEST(v1.size() == 6);

        v1.emplace_back(6);
        BOOST_TEST(v1.size() == 7);
    }
    {
        toml::value v1(42);
        BOOST_CHECK_THROW(v1.size(),          toml::type_error);
        BOOST_CHECK_THROW(v1.push_back(1),    toml::type_error);
        BOOST_CHECK_THROW(v1.emplace_back(1), toml::type_error);
    }
}
