#include <toml/utility.hpp>

#include "unit_test.hpp"

#include <array>
#include <vector>

BOOST_AUTO_TEST_CASE(test_try_reserve)
{
    {
        // since BOOST_TEST is a macro, it cannot handle commas correctly.
        // When toml::detail::has_reserve_method<std::array<int, 1>>::value
        // is passed to a macro, C preprocessor considers
        // toml::detail::has_reserve_method<std::array<int as the first argument
        // and 1>>::value as the second argument. We need an alias to avoid
        // this problem.
        using reservable_type    = std::vector<int>  ;
        using nonreservable_type = std::array<int, 1>;
        BOOST_TEST( toml::detail::has_reserve_method<reservable_type   >::value);
        BOOST_TEST(!toml::detail::has_reserve_method<nonreservable_type>::value);
    }
    {
        std::vector<int> v;
        toml::try_reserve(v, 100);
        BOOST_TEST(v.capacity() == 100u);
    }
}

BOOST_AUTO_TEST_CASE(test_concat_to_string)
{
    const std::string cat = toml::concat_to_string("foo", "bar", 42);
    BOOST_TEST(cat == "foobar42");
}

BOOST_AUTO_TEST_CASE(test_from_string)
{
    {
        const std::string str("123");
        BOOST_TEST(toml::from_string<int>(str, 0) == 123);
    }
    {
        const std::string str("01");
        BOOST_TEST(toml::from_string<int>(str, 0) == 1);
    }
}
