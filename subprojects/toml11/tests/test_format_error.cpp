#include <toml.hpp>

#include "unit_test.hpp"

#include <iostream>

// to check it successfully compiles. it does not check the formatted string.

BOOST_AUTO_TEST_CASE(test_1_value)
{
    toml::value val(42);

    {
        const std::string pretty_error =
            toml::format_error("[error] test error", val, "this is a value");
        std::cout << pretty_error << std::endl;
    }

    {
        const std::string pretty_error =
            toml::format_error("[error] test error", val, "this is a value",
                               {"this is a hint"});
        std::cout << pretty_error << std::endl;
    }
}

BOOST_AUTO_TEST_CASE(test_2_values)
{
    toml::value v1(42);
    toml::value v2(3.14);
    {
        const std::string pretty_error =
            toml::format_error("[error] test error with two values",
                               v1, "this is the answer",
                               v2, "this is the pi");
        std::cout << pretty_error << std::endl;
    }

    {
        const std::string pretty_error =
            toml::format_error("[error] test error with two values",
                               v1, "this is the answer",
                               v2, "this is the pi",
                               {"hint"});
        std::cout << pretty_error << std::endl;
    }
}

BOOST_AUTO_TEST_CASE(test_3_values)
{
    toml::value v1(42);
    toml::value v2(3.14);
    toml::value v3("foo");
    {
        const std::string pretty_error =
            toml::format_error("[error] test error with two values",
                               v1, "this is the answer",
                               v2, "this is the pi",
                               v3, "this is a meta-syntactic variable");
        std::cout << pretty_error << std::endl;
    }

    {
        const std::string pretty_error =
            toml::format_error("[error] test error with two values",
                               v1, "this is the answer",
                               v2, "this is the pi",
                               v3, "this is a meta-syntactic variable",
                               {"hint 1", "hint 2"});
        std::cout << pretty_error << std::endl;
    }
}
