#include <toml/result.hpp>

#include "unit_test.hpp"

#include <iostream>

BOOST_AUTO_TEST_CASE(test_construct)
{
    {
        auto s = toml::ok(42);
        toml::result<int, std::string> result(s);
        BOOST_TEST(!!result);
        BOOST_TEST(result.is_ok());
        BOOST_TEST(!result.is_err());
        BOOST_TEST(result.unwrap() == 42);
    }
    {
        const auto s = toml::ok(42);
        toml::result<int, std::string> result(s);
        BOOST_TEST(!!result);
        BOOST_TEST(result.is_ok());
        BOOST_TEST(!result.is_err());
        BOOST_TEST(result.unwrap() == 42);
    }
    {
        toml::result<int, std::string> result(toml::ok(42));
        BOOST_TEST(!!result);
        BOOST_TEST(result.is_ok());
        BOOST_TEST(!result.is_err());
        BOOST_TEST(result.unwrap() == 42);
    }

    {
        auto f = toml::err<std::string>("foobar");
        toml::result<int, std::string> result(f);
        BOOST_TEST(!result);
        BOOST_TEST(!result.is_ok());
        BOOST_TEST(result.is_err());
        BOOST_TEST(result.unwrap_err() == "foobar");
    }
    {
        const auto f = toml::err<std::string>("foobar");
        toml::result<int, std::string> result(f);
        BOOST_TEST(!result);
        BOOST_TEST(!result.is_ok());
        BOOST_TEST(result.is_err());
        BOOST_TEST(result.unwrap_err() == "foobar");
    }
    {
        toml::result<int, std::string> result(toml::err<std::string>("foobar"));
        BOOST_TEST(!result);
        BOOST_TEST(!result.is_ok());
        BOOST_TEST(result.is_err());
        BOOST_TEST(result.unwrap_err() == "foobar");
    }
}

BOOST_AUTO_TEST_CASE(test_assignment)
{
    {
        toml::result<int, std::string> result(toml::err<std::string>("foobar"));
        result = toml::ok(42);
        BOOST_TEST(!!result);
        BOOST_TEST(result.is_ok());
        BOOST_TEST(!result.is_err());
        BOOST_TEST(result.unwrap() == 42);
    }
    {
        toml::result<int, std::string> result(toml::err<std::string>("foobar"));
        auto s = toml::ok(42);
        result = s;
        BOOST_TEST(!!result);
        BOOST_TEST(result.is_ok());
        BOOST_TEST(!result.is_err());
        BOOST_TEST(result.unwrap() == 42);
    }
    {
        toml::result<int, std::string> result(toml::err<std::string>("foobar"));
        const auto s = toml::ok(42);
        result = s;
        BOOST_TEST(!!result);
        BOOST_TEST(result.is_ok());
        BOOST_TEST(!result.is_err());
        BOOST_TEST(result.unwrap() == 42);
    }
    {
        toml::result<int, std::string> result(toml::err<std::string>("foobar"));
        result = toml::err<std::string>("hoge");
        BOOST_TEST(!result);
        BOOST_TEST(!result.is_ok());
        BOOST_TEST(result.is_err());
        BOOST_TEST(result.unwrap_err() == "hoge");
    }
    {
        toml::result<int, std::string> result(toml::err<std::string>("foobar"));
        auto f = toml::err<std::string>("hoge");
        result = f;
        BOOST_TEST(!result);
        BOOST_TEST(!result.is_ok());
        BOOST_TEST(result.is_err());
        BOOST_TEST(result.unwrap_err() == "hoge");
    }
    {
        toml::result<int, std::string> result(toml::err<std::string>("foobar"));
        const auto f = toml::err<std::string>("hoge");
        result = f;
        BOOST_TEST(!result);
        BOOST_TEST(!result.is_ok());
        BOOST_TEST(result.is_err());
        BOOST_TEST(result.unwrap_err() == "hoge");
    }
}

BOOST_AUTO_TEST_CASE(test_map)
{
    {
        const toml::result<int, std::string> result(toml::ok(42));
        const auto mapped = result.map(
                [](const int i) -> int {
                    return i * 2;
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(mapped.unwrap() == 42 * 2);
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::ok(std::unique_ptr<int>(new int(42))));
        const auto mapped = std::move(result).map(
                [](std::unique_ptr<int> i) -> int {
                    return *i;
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(mapped.unwrap() == 42);
    }
    {
        const toml::result<int, std::string> result(toml::err<std::string>("hoge"));
        const auto mapped = result.map(
                [](const int i) -> int {
                    return i * 2;
                });

        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hoge");
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::err<std::string>("hoge"));
        const auto mapped = std::move(result).map(
                [](std::unique_ptr<int> i) -> int {
                    return *i;
                });

        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hoge");
    }
}

BOOST_AUTO_TEST_CASE(test_map_err)
{
    {
        const toml::result<int, std::string> result(toml::ok(42));
        const auto mapped = result.map_err(
                [](const std::string s) -> std::string {
                    return s + s;
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(mapped.unwrap() == 42);
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::ok(std::unique_ptr<int>(new int(42))));
        const auto mapped = std::move(result).map_err(
                [](const std::string s) -> std::string {
                    return s + s;
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(*(mapped.unwrap()) == 42);
    }
    {
        const toml::result<int, std::string> result(toml::err<std::string>("hoge"));
        const auto mapped = result.map_err(
                [](const std::string s) -> std::string {
                    return s + s;
                });
        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hogehoge");
    }
    {
        toml::result<int, std::unique_ptr<std::string>>
            result(toml::err(std::unique_ptr<std::string>(new std::string("hoge"))));
        const auto mapped = std::move(result).map_err(
                [](std::unique_ptr<std::string> p) -> std::string {
                    return *p;
                });

        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hoge");
    }
}

BOOST_AUTO_TEST_CASE(test_map_or_else)
{
    {
        const toml::result<int, std::string> result(toml::ok(42));
        const auto mapped = result.map_or_else(
                [](const int i) -> int {
                    return i * 2;
                }, 54);

        BOOST_TEST(mapped == 42 * 2);
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::ok(std::unique_ptr<int>(new int(42))));
        const auto mapped = std::move(result).map_or_else(
                [](std::unique_ptr<int> i) -> int {
                    return *i;
                }, 54);

        BOOST_TEST(mapped == 42);
    }
    {
        const toml::result<int, std::string> result(toml::err<std::string>("hoge"));
        const auto mapped = result.map_or_else(
                [](const int i) -> int {
                    return i * 2;
                }, 54);

        BOOST_TEST(mapped == 54);
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::err<std::string>("hoge"));
        const auto mapped = std::move(result).map_or_else(
                [](std::unique_ptr<int> i) -> int {
                    return *i;
                }, 54);

        BOOST_TEST(mapped == 54);
    }
}

BOOST_AUTO_TEST_CASE(test_map_err_or_else)
{
    {
        const toml::result<int, std::string> result(toml::ok(42));
        const auto mapped = result.map_err_or_else(
                [](const std::string i) -> std::string {
                    return i + i;
                }, "foobar");

        BOOST_TEST(mapped == "foobar");
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::ok(std::unique_ptr<int>(new int(42))));
        const auto mapped = std::move(result).map_err_or_else(
                [](const std::string i) -> std::string {
                    return i + i;
                }, "foobar");

        BOOST_TEST(mapped == "foobar");
    }
    {
        const toml::result<int, std::string> result(toml::err<std::string>("hoge"));
        const auto mapped = result.map_err_or_else(
                [](const std::string i) -> std::string {
                    return i + i;
                }, "foobar");

        BOOST_TEST(mapped == "hogehoge");
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::err<std::string>("hoge"));
        const auto mapped = result.map_err_or_else(
                [](const std::string i) -> std::string {
                    return i + i;
                }, "foobar");

        BOOST_TEST(mapped == "hogehoge");
    }
}


BOOST_AUTO_TEST_CASE(test_and_then)
{
    {
        const toml::result<int, std::string> result(toml::ok(42));
        const auto mapped = result.and_then(
                [](const int i) -> toml::result<int, std::string> {
                    return toml::ok(i * 2);
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(mapped.unwrap() == 42 * 2);
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::ok(std::unique_ptr<int>(new int(42))));
        const auto mapped = std::move(result).and_then(
                [](std::unique_ptr<int> i) -> toml::result<int, std::string> {
                    return toml::ok(*i);
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(mapped.unwrap() == 42);
    }
    {
        const toml::result<int, std::string> result(toml::err<std::string>("hoge"));
        const auto mapped = result.and_then(
                [](const int i) -> toml::result<int, std::string> {
                    return toml::ok(i * 2);
                });

        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hoge");
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::err<std::string>("hoge"));
        const auto mapped = std::move(result).and_then(
                [](std::unique_ptr<int> i) -> toml::result<int, std::string> {
                    return toml::ok(*i);
                });

        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hoge");
    }
}

BOOST_AUTO_TEST_CASE(test_or_else)
{
    {
        const toml::result<int, std::string> result(toml::ok(42));
        const auto mapped = result.or_else(
                [](const std::string& s) -> toml::result<int, std::string> {
                    return toml::err(s + s);
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(mapped.unwrap() == 42);
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::ok(std::unique_ptr<int>(new int(42))));
        const auto mapped = std::move(result).or_else(
                [](const std::string& s) -> toml::result<std::unique_ptr<int>, std::string> {
                    return toml::err(s + s);
                });

        BOOST_TEST(!!mapped);
        BOOST_TEST(mapped.is_ok());
        BOOST_TEST(!mapped.is_err());
        BOOST_TEST(*mapped.unwrap() == 42);
    }
    {
        const toml::result<int, std::string> result(toml::err<std::string>("hoge"));
        const auto mapped = result.or_else(
                [](const std::string& s) -> toml::result<int, std::string> {
                    return toml::err(s + s);
                });

        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hogehoge");
    }
    {
        toml::result<std::unique_ptr<int>, std::string>
            result(toml::err<std::string>("hoge"));
        const auto mapped = std::move(result).or_else(
                [](const std::string& s) -> toml::result<std::unique_ptr<int>, std::string> {
                    return toml::err(s + s);
                });

        BOOST_TEST(!mapped);
        BOOST_TEST(!mapped.is_ok());
        BOOST_TEST(mapped.is_err());
        BOOST_TEST(mapped.unwrap_err() == "hogehoge");
    }
}

BOOST_AUTO_TEST_CASE(test_and_or_other)
{
    {
        const toml::result<int, std::string> r1(toml::ok(42));
        const toml::result<int, std::string> r2(toml::err<std::string>("foo"));

        BOOST_TEST(r1 ==    r1.or_other(r2));
        BOOST_TEST(r2 ==    r1.and_other(r2));
        BOOST_TEST(42 ==    r1.or_other(r2).unwrap());
        BOOST_TEST("foo" == r1.and_other(r2).unwrap_err());
    }
    {
        auto r1_gen = []() -> toml::result<int, std::string> {
            return toml::ok(42);
        };
        auto r2_gen = []() -> toml::result<int, std::string> {
            return toml::err<std::string>("foo");
        };
        const auto r3 = r1_gen();
        const auto r4 = r2_gen();

        BOOST_TEST(r3 == r1_gen().or_other (r2_gen()));
        BOOST_TEST(r4 == r1_gen().and_other(r2_gen()));
        BOOST_TEST(42 ==    r1_gen().or_other (r2_gen()).unwrap());
        BOOST_TEST("foo" == r1_gen().and_other(r2_gen()).unwrap_err());
    }
}
