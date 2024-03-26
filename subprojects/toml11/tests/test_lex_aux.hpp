#include <iostream>
#include <iomanip>
#include <algorithm>
#include <toml/region.hpp>
#include <toml/result.hpp>

#define TOML11_TEST_LEX_ACCEPT(lxr, tkn, expct)                                \
do {                                                                           \
    const std::string token   (tkn);                                           \
    const std::string expected(expct);                                         \
    toml::detail::location loc("test", token);                                 \
    const auto result = lxr::invoke(loc);                                      \
    BOOST_TEST(result.is_ok());                                                \
    if(result.is_ok()){                                                        \
        const auto region = result.unwrap();                                   \
        BOOST_TEST(region.str() == expected);                                  \
        BOOST_TEST(region.str().size() == expected.size());                    \
        BOOST_TEST(static_cast<std::size_t>(std::distance(                     \
                        loc.begin(), loc.iter())) == region.size());           \
    } else {                                                                   \
        std::cerr << "lexer failed with input `";                              \
        std::cerr << token << "`. expected `" << expected << "`\n";            \
        std::cerr << "reason: " << result.unwrap_err() << '\n';                \
    }                                                                          \
} while(false);                                                                \
/**/

#define TOML11_TEST_LEX_REJECT(lxr, tkn)                                       \
do {                                                                           \
    const std::string token   (tkn);                                           \
    toml::detail::location loc("test", token);                                 \
    const auto result = lxr::invoke(loc);                                      \
    BOOST_TEST(result.is_err());                                               \
    const bool loc_same = (loc.begin() == loc.iter());                         \
    BOOST_TEST(loc_same);                                                      \
} while(false); /**/
