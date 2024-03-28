#include <toml.hpp>

#include "unit_test.hpp"

#include <map>

BOOST_AUTO_TEST_CASE(test_file_as_literal)
{
    using namespace toml::literals::toml_literals;

    {
        const toml::value r{{"a", 42}, {"b", "baz"}};
        const toml::value v = R"(
            a = 42
            b = "baz"
        )"_toml;

        BOOST_TEST(r == v);
    }
    {
        const toml::value r{
            {"c", 3.14},
            {"table", toml::table{{"a", 42}, {"b", "baz"}}}
        };
        const toml::value v = R"(
            c = 3.14
            [table]
            a = 42
            b = "baz"
        )"_toml;

        BOOST_TEST(r == v);
    }
    {
        const toml::value r{
            {"table", toml::table{{"a", 42}, {"b", "baz"}}}
        };
        const toml::value v = R"(
            [table]
            a = 42
            b = "baz"
        )"_toml;

        BOOST_TEST(r == v);
    }
    {
        const toml::value r{
            {"array_of_tables", toml::array{toml::table{}}}
        };
        const toml::value v = R"(
            [[array_of_tables]]
        )"_toml;

        BOOST_TEST(r == v);
    }
}

BOOST_AUTO_TEST_CASE(test_value_as_literal)
{
    using namespace toml::literals::toml_literals;

    {
        const toml::value v1 = "true"_toml;
        const toml::value v2 = "false"_toml;

        BOOST_TEST(v1.is_boolean());
        BOOST_TEST(v2.is_boolean());
        BOOST_TEST(toml::get<bool>(v1));
        BOOST_TEST(!toml::get<bool>(v2));
    }
    {
        const toml::value v1 = "123_456"_toml;
        const toml::value v2 = "0b0010"_toml;
        const toml::value v3 = "0xDEADBEEF"_toml;

        BOOST_TEST(v1.is_integer());
        BOOST_TEST(v2.is_integer());
        BOOST_TEST(v3.is_integer());
        BOOST_TEST(toml::get<toml::integer>(v1) == 123456);
        BOOST_TEST(toml::get<toml::integer>(v2) == 2);
        BOOST_TEST(toml::get<toml::integer>(v3) == 0xDEADBEEF);
    }
    {
        const toml::value v1 = "3.1415"_toml;
        const toml::value v2 = "6.02e+23"_toml;

        BOOST_TEST(v1.is_floating());
        BOOST_TEST(v2.is_floating());
        BOOST_TEST(toml::get<double>(v1) == 3.1415,  boost::test_tools::tolerance(0.00001));
        BOOST_TEST(toml::get<double>(v2) == 6.02e23, boost::test_tools::tolerance(0.0001));
    }
    {
        const toml::value v1 = R"("foo")"_toml;
        const toml::value v2 = R"('foo')"_toml;
        const toml::value v3 = R"("""foo""")"_toml;
        const toml::value v4 = R"('''foo''')"_toml;

        BOOST_TEST(v1.is_string());
        BOOST_TEST(v2.is_string());
        BOOST_TEST(v3.is_string());
        BOOST_TEST(v4.is_string());
        BOOST_TEST(toml::get<std::string>(v1) == "foo");
        BOOST_TEST(toml::get<std::string>(v2) == "foo");
        BOOST_TEST(toml::get<std::string>(v3) == "foo");
        BOOST_TEST(toml::get<std::string>(v4) == "foo");
    }
    {
        {
            const toml::value v1 = R"([1,2,3])"_toml;
            BOOST_TEST(v1.is_array());
            const bool result = (toml::get<std::vector<int>>(v1) == std::vector<int>{1,2,3});
            BOOST_TEST(result);
        }
        {
            const toml::value v2 = R"([1,])"_toml;
            BOOST_TEST(v2.is_array());
            const bool result = (toml::get<std::vector<int>>(v2) == std::vector<int>{1});
            BOOST_TEST(result);
        }
        {
            const toml::value v3 = R"([[1,]])"_toml;
            BOOST_TEST(v3.is_array());
            const bool result = (toml::get<std::vector<int>>(toml::get<toml::array>(v3).front()) == std::vector<int>{1});
            BOOST_TEST(result);
        }
        {
            const toml::value v4 = R"([[1],])"_toml;
            BOOST_TEST(v4.is_array());
            const bool result = (toml::get<std::vector<int>>(toml::get<toml::array>(v4).front()) == std::vector<int>{1});
            BOOST_TEST(result);
        }
    }
    {
        const toml::value v1 = R"({a = 42})"_toml;

        BOOST_TEST(v1.is_table());
        const bool result = toml::get<std::map<std::string,int>>(v1) ==
                               std::map<std::string,int>{{"a", 42}};
        BOOST_TEST(result);
    }
    {
        const toml::value v1 = "1979-05-27"_toml;

        BOOST_TEST(v1.is_local_date());
        BOOST_TEST(toml::get<toml::local_date>(v1) ==
                   toml::local_date(1979, toml::month_t::May, 27));
    }
    {
        const toml::value v1 = "12:00:00"_toml;

        BOOST_TEST(v1.is_local_time());
        const bool result = toml::get<std::chrono::hours>(v1) == std::chrono::hours(12);
        BOOST_TEST(result);
    }
    {
        const toml::value v1 = "1979-05-27T07:32:00"_toml;
        BOOST_TEST(v1.is_local_datetime());
        BOOST_TEST(toml::get<toml::local_datetime>(v1) ==
            toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                 toml::local_time(7, 32, 0)));
    }
    {
        const toml::value v1 = "1979-05-27T07:32:00Z"_toml;
        BOOST_TEST(v1.is_offset_datetime());
        BOOST_TEST(toml::get<toml::offset_datetime>(v1) ==
            toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                  toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
    }
}

BOOST_AUTO_TEST_CASE(test_file_as_u8_literal)
{
    using namespace toml::literals::toml_literals;

    {
        const toml::value r{{"a", 42}, {"b", "baz"}};
        const toml::value v = u8R"(
            a = 42
            b = "baz"
        )"_toml;

        BOOST_TEST(r == v);
    }
    {
        const toml::value r{
            {"c", 3.14},
            {"table", toml::table{{"a", 42}, {"b", "baz"}}}
        };
        const toml::value v = u8R"(
            c = 3.14
            [table]
            a = 42
            b = "baz"
        )"_toml;

        BOOST_TEST(r == v);
    }
    {
        const toml::value r{
            {"table", toml::table{{"a", 42}, {"b", "baz"}}}
        };
        const toml::value v = u8R"(
            [table]
            a = 42
            b = "baz"
        )"_toml;

        BOOST_TEST(r == v);
    }
    {
        const toml::value r{
            {"array_of_tables", toml::array{toml::table{}}}
        };
        const toml::value v = u8R"(
            [[array_of_tables]]
        )"_toml;

        BOOST_TEST(r == v);
    }
}

BOOST_AUTO_TEST_CASE(test_value_as_u8_literal)
{
    using namespace toml::literals::toml_literals;

    {
        const toml::value v1 = u8"true"_toml;
        const toml::value v2 = u8"false"_toml;

        BOOST_TEST(v1.is_boolean());
        BOOST_TEST(v2.is_boolean());
        BOOST_TEST(toml::get<bool>(v1));
        BOOST_TEST(!toml::get<bool>(v2));
    }
    {
        const toml::value v1 = u8"123_456"_toml;
        const toml::value v2 = u8"0b0010"_toml;
        const toml::value v3 = u8"0xDEADBEEF"_toml;

        BOOST_TEST(v1.is_integer());
        BOOST_TEST(v2.is_integer());
        BOOST_TEST(v3.is_integer());
        BOOST_TEST(toml::get<toml::integer>(v1) == 123456);
        BOOST_TEST(toml::get<toml::integer>(v2) == 2);
        BOOST_TEST(toml::get<toml::integer>(v3) == 0xDEADBEEF);
    }
    {
        const toml::value v1 = u8"3.1415"_toml;
        const toml::value v2 = u8"6.02e+23"_toml;

        BOOST_TEST(v1.is_floating());
        BOOST_TEST(v2.is_floating());
        BOOST_TEST(toml::get<double>(v1) == 3.1415,  boost::test_tools::tolerance(0.00001));
        BOOST_TEST(toml::get<double>(v2) == 6.02e23, boost::test_tools::tolerance(0.0001));
    }
    {
        const toml::value v1 = u8R"("foo")"_toml;
        const toml::value v2 = u8R"('foo')"_toml;
        const toml::value v3 = u8R"("""foo""")"_toml;
        const toml::value v4 = u8R"('''foo''')"_toml;
        const toml::value v5 = u8R"("ひらがな")"_toml;

        BOOST_TEST(v1.is_string());
        BOOST_TEST(v2.is_string());
        BOOST_TEST(v3.is_string());
        BOOST_TEST(v4.is_string());
        BOOST_TEST(v5.is_string());
        BOOST_TEST(toml::get<std::string>(v1) == "foo");
        BOOST_TEST(toml::get<std::string>(v2) == "foo");
        BOOST_TEST(toml::get<std::string>(v3) == "foo");
        BOOST_TEST(toml::get<std::string>(v4) == "foo");
        BOOST_TEST(toml::get<std::string>(v5) == "\xE3\x81\xB2\xE3\x82\x89\xE3\x81\x8C\xE3\x81\xAA");
    }
    {
        {
            const toml::value v1 = u8R"([1,2,3])"_toml;
            BOOST_TEST(v1.is_array());
            const bool result = (toml::get<std::vector<int>>(v1) == std::vector<int>{1,2,3});
            BOOST_TEST(result);
        }
        {
            const toml::value v2 = u8R"([1,])"_toml;
            BOOST_TEST(v2.is_array());
            const bool result = (toml::get<std::vector<int>>(v2) == std::vector<int>{1});
            BOOST_TEST(result);
        }
        {
            const toml::value v3 = u8R"([[1,]])"_toml;
            BOOST_TEST(v3.is_array());
            const bool result = (toml::get<std::vector<int>>(toml::get<toml::array>(v3).front()) == std::vector<int>{1});
            BOOST_TEST(result);
        }
        {
            const toml::value v4 = u8R"([[1],])"_toml;
            BOOST_TEST(v4.is_array());
            const bool result = (toml::get<std::vector<int>>(toml::get<toml::array>(v4).front()) == std::vector<int>{1});
            BOOST_TEST(result);
        }
    }
    {
        const toml::value v1 = u8R"({a = 42})"_toml;

        BOOST_TEST(v1.is_table());
        const bool result = toml::get<std::map<std::string,int>>(v1) ==
                               std::map<std::string,int>{{"a", 42}};
        BOOST_TEST(result);
    }
    {
        const toml::value v1 = u8"1979-05-27"_toml;

        BOOST_TEST(v1.is_local_date());
        BOOST_TEST(toml::get<toml::local_date>(v1) ==
                   toml::local_date(1979, toml::month_t::May, 27));
    }
    {
        const toml::value v1 = u8"12:00:00"_toml;

        BOOST_TEST(v1.is_local_time());
        const bool result = toml::get<std::chrono::hours>(v1) == std::chrono::hours(12);
        BOOST_TEST(result);
    }
    {
        const toml::value v1 = u8"1979-05-27T07:32:00"_toml;
        BOOST_TEST(v1.is_local_datetime());
        BOOST_TEST(toml::get<toml::local_datetime>(v1) ==
            toml::local_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                 toml::local_time(7, 32, 0)));
    }
    {
        const toml::value v1 = u8"1979-05-27T07:32:00Z"_toml;
        BOOST_TEST(v1.is_offset_datetime());
        BOOST_TEST(toml::get<toml::offset_datetime>(v1) ==
            toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                  toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
    }
}
