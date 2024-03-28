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

namespace test
{
// to compare result values in BOOST_TEST().
//
// BOOST_TEST outputs the expected and actual values. Thus it includes the
// output stream operator inside. To compile it, we need operator<<s for
// containers to compare.
template<typename charT, typename traits, typename T, typename Alloc>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const std::vector<T, Alloc>& v)
{
    os << "[ ";
    for(const auto& i : v) {os << i << ' ';}
    os << ']';
    return os;
}
template<typename charT, typename traits, typename T, typename Alloc>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const std::deque<T, Alloc>& v)
{
    os << "[ ";
    for(const auto& i : v) {os << i << ' ';}
    os << ']';
    return os;
}
template<typename charT, typename traits, typename T, typename Alloc>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os, const std::list<T, Alloc>& v)
{
    os << "[ ";
    for(const auto& i : v) {os << i << ' ';}
    os << ']';
    return os;
}
template<typename charT, typename traits,
         typename Key, typename Value, typename Comp, typename Alloc>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os,
           const std::map<Key, Value, Comp, Alloc>& v)
{
    os << "[ ";
    for(const auto& i : v) {os << '{' << i.first << ", " << i.second << "} ";}
    os << ']';
    return os;
}
template<typename charT, typename traits,
         typename Key, typename Value, typename Hash, typename Eq, typename Alloc>
std::basic_ostream<charT, traits>&
operator<<(std::basic_ostream<charT, traits>& os,
           const std::unordered_map<Key, Value, Hash, Eq, Alloc>& v)
{
    os << "[ ";
    for(const auto& i : v) {os << '{' << i.first << ", " << i.second << "} ";}
    os << ']';
    return os;
}
} // test

#define TOML11_TEST_GET_OR_EXACT(toml_type, init_expr, opt_expr)\
    {                                                           \
        using namespace test;                                   \
        const toml::toml_type init init_expr ;                  \
        const toml::toml_type opt  opt_expr ;                   \
        const value_type v(init);                               \
        BOOST_TEST(init != opt);                                \
        BOOST_TEST(init == toml::get_or(v, opt));               \
    }                                                           \
    /**/

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_or_exact, value_type, test_value_types)
{
    TOML11_TEST_GET_OR_EXACT(boolean,         ( true), (false))
    TOML11_TEST_GET_OR_EXACT(integer,         (   42), (   54))
    TOML11_TEST_GET_OR_EXACT(floating,        ( 3.14), ( 2.71))
    TOML11_TEST_GET_OR_EXACT(string,          ("foo"), ("bar"))
    TOML11_TEST_GET_OR_EXACT(local_time,      (12, 30, 45), (6, 0, 30))
    TOML11_TEST_GET_OR_EXACT(local_date,      (2019, toml::month_t::Apr, 1),
                                              (1999, toml::month_t::Jan, 2))
    TOML11_TEST_GET_OR_EXACT(local_datetime,
            (toml::local_date(2019, toml::month_t::Apr, 1), toml::local_time(12, 30, 45)),
            (toml::local_date(1999, toml::month_t::Jan, 2), toml::local_time( 6,  0, 30))
            )
    TOML11_TEST_GET_OR_EXACT(offset_datetime,
            (toml::local_date(2019, toml::month_t::Apr, 1), toml::local_time(12, 30, 45), toml::time_offset( 9, 0)),
            (toml::local_date(1999, toml::month_t::Jan, 2), toml::local_time( 6,  0, 30), toml::time_offset(-3, 0))
            )
    {
        const typename value_type::array_type init{1,2,3,4,5};
        const typename value_type::array_type opt {6,7,8,9,10};
        const value_type v(init);
        BOOST_TEST(init != opt);
        BOOST_TEST(init == toml::get_or(v, opt));
    }
    {
        const typename value_type::table_type init{{"key1", 42}, {"key2", "foo"}};
        const typename value_type::table_type opt {{"key1", 54}, {"key2", "bar"}};
        const value_type v(init);
        BOOST_TEST(init != opt);
        BOOST_TEST(init == toml::get_or(v, opt));
    }
}
#undef TOML11_TEST_GET_OR_EXACT

#define TOML11_TEST_GET_OR_MOVE_EXACT(toml_type, init_expr, opt_expr)\
    {                                                                \
        using namespace test;                                        \
        const toml::toml_type init init_expr ;                       \
        toml::toml_type       opt  opt_expr ;                        \
        value_type v(init);                                          \
        BOOST_TEST(init != opt);                                     \
        const auto opt_ = toml::get_or(std::move(v), std::move(opt));\
        BOOST_TEST(init == opt_);                                    \
    }                                                                \
    /**/

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_or_move, value_type, test_value_types)
{
    TOML11_TEST_GET_OR_MOVE_EXACT(boolean,         ( true), (false))
    TOML11_TEST_GET_OR_MOVE_EXACT(integer,         (   42), (   54))
    TOML11_TEST_GET_OR_MOVE_EXACT(floating,        ( 3.14), ( 2.71))
    TOML11_TEST_GET_OR_MOVE_EXACT(string,          ("foo"), ("bar"))
    TOML11_TEST_GET_OR_MOVE_EXACT(local_time,      (12, 30, 45), (6, 0, 30))
    TOML11_TEST_GET_OR_MOVE_EXACT(local_date,      (2019, toml::month_t::Apr, 1),
                                                   (1999, toml::month_t::Jan, 2))
    TOML11_TEST_GET_OR_MOVE_EXACT(local_datetime,
            (toml::local_date(2019, toml::month_t::Apr, 1), toml::local_time(12, 30, 45)),
            (toml::local_date(1999, toml::month_t::Jan, 2), toml::local_time( 6,  0, 30))
            )
    TOML11_TEST_GET_OR_MOVE_EXACT(offset_datetime,
            (toml::local_date(2019, toml::month_t::Apr, 1), toml::local_time(12, 30, 45), toml::time_offset( 9, 0)),
            (toml::local_date(1999, toml::month_t::Jan, 2), toml::local_time( 6,  0, 30), toml::time_offset(-3, 0))
            )
    {
        const typename value_type::array_type init{1,2,3,4,5};
        typename value_type::array_type opt {6,7,8,9,10};
        value_type v(init);
        BOOST_TEST(init != opt);
        const auto opt_ = toml::get_or(std::move(v), std::move(opt));
        BOOST_TEST(init == opt_);
    }
    {
        const typename value_type::table_type init{{"key1", 42}, {"key2", "foo"}};
        typename value_type::table_type opt {{"key1", 54}, {"key2", "bar"}};
        value_type v(init);
        BOOST_TEST(init != opt);
        const auto opt_ = toml::get_or(std::move(v), std::move(opt));
        BOOST_TEST(init == opt_);
    }
}
#undef TOML11_TEST_GET_OR_MOVE_EXACT


#define TOML11_TEST_GET_OR_MODIFY(toml_type, init_expr, opt_expr)\
    {                                                            \
        using namespace test;                                    \
        const toml::toml_type init init_expr ;                   \
        toml::toml_type       opt1 opt_expr ;                    \
        toml::toml_type       opt2 opt_expr ;                    \
        value_type v(init);                                      \
        BOOST_TEST(init != opt1);                                \
        toml::get_or(v, opt2) = opt1;                            \
        BOOST_TEST(opt1 == toml::get<toml::toml_type>(v));       \
    }                                                            \
    /**/
BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_or_modify, value_type, test_value_types)
{
    TOML11_TEST_GET_OR_MODIFY(boolean,         ( true), (false))
    TOML11_TEST_GET_OR_MODIFY(integer,         (   42), (   54))
    TOML11_TEST_GET_OR_MODIFY(floating,        ( 3.14), ( 2.71))
    TOML11_TEST_GET_OR_MODIFY(string,          ("foo"), ("bar"))
    TOML11_TEST_GET_OR_MODIFY(local_time,      (12, 30, 45), (6, 0, 30))
    TOML11_TEST_GET_OR_MODIFY(local_date,      (2019, toml::month_t::Apr, 1),
                                              (1999, toml::month_t::Jan, 2))
    TOML11_TEST_GET_OR_MODIFY(local_datetime,
            (toml::local_date(2019, toml::month_t::Apr, 1), toml::local_time(12, 30, 45)),
            (toml::local_date(1999, toml::month_t::Jan, 2), toml::local_time( 6,  0, 30))
            )
    TOML11_TEST_GET_OR_MODIFY(offset_datetime,
            (toml::local_date(2019, toml::month_t::Apr, 1), toml::local_time(12, 30, 45), toml::time_offset( 9, 0)),
            (toml::local_date(1999, toml::month_t::Jan, 2), toml::local_time( 6,  0, 30), toml::time_offset(-3, 0))
            )
    {
        typename value_type::array_type init{1,2,3,4,5};
        typename value_type::array_type opt1{6,7,8,9,10};
        typename value_type::array_type opt2{6,7,8,9,10};
        BOOST_TEST(init != opt1);
        value_type v(init);
        toml::get_or(v, opt2) = opt1;
        BOOST_TEST(opt1 == toml::get<typename value_type::array_type>(v));
    }
    {
        typename value_type::table_type init{{"key1", 42}, {"key2", "foo"}};
        typename value_type::table_type opt1{{"key1", 54}, {"key2", "bar"}};
        typename value_type::table_type opt2{{"key1", 54}, {"key2", "bar"}};
        value_type v(init);
        BOOST_TEST(init != opt1);
        toml::get_or(v, opt2) = opt1;
        BOOST_TEST(opt1 == toml::get<typename value_type::table_type>(v));
    }
}
#undef TOML11_TEST_GET_OR_MODIFY

#define TOML11_TEST_GET_OR_FALLBACK(init_type, opt_type)  \
    {                                                     \
        using namespace test;                             \
        value_type v(init_type);                          \
        BOOST_TEST(opt_type == toml::get_or(v, opt_type));\
    }                                                     \
    /**/

BOOST_AUTO_TEST_CASE_TEMPLATE(test_get_or_fallback, value_type, test_value_types)
{
    const toml::boolean         boolean        (true);
    const toml::integer         integer        (42);
    const toml::floating        floating       (3.14);
    const toml::string          string         ("foo");
    const toml::local_time      local_time     (12, 30, 45);
    const toml::local_date      local_date     (2019, toml::month_t::Apr, 1);
    const toml::local_datetime  local_datetime (
            toml::local_date(2019, toml::month_t::Apr, 1),
            toml::local_time(12, 30, 45));
    const toml::offset_datetime offset_datetime(
            toml::local_date(2019, toml::month_t::Apr, 1),
            toml::local_time(12, 30, 45), toml::time_offset( 9, 0));

    using array_type = typename value_type::array_type;
    using table_type = typename value_type::table_type;
    const array_type array{1, 2, 3, 4, 5};
    const table_type table{{"key1", 42}, {"key2", "foo"}};

    TOML11_TEST_GET_OR_FALLBACK(boolean, integer        );
    TOML11_TEST_GET_OR_FALLBACK(boolean, floating       );
    TOML11_TEST_GET_OR_FALLBACK(boolean, string         );
    TOML11_TEST_GET_OR_FALLBACK(boolean, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(boolean, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(boolean, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(boolean, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(boolean, array          );
    TOML11_TEST_GET_OR_FALLBACK(boolean, table          );

    TOML11_TEST_GET_OR_FALLBACK(integer, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(integer, floating       );
    TOML11_TEST_GET_OR_FALLBACK(integer, string         );
    TOML11_TEST_GET_OR_FALLBACK(integer, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(integer, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(integer, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(integer, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(integer, array          );
    TOML11_TEST_GET_OR_FALLBACK(integer, table          );

    TOML11_TEST_GET_OR_FALLBACK(floating, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(floating, integer        );
    TOML11_TEST_GET_OR_FALLBACK(floating, string         );
    TOML11_TEST_GET_OR_FALLBACK(floating, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(floating, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(floating, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(floating, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(floating, array          );
    TOML11_TEST_GET_OR_FALLBACK(floating, table          );

    TOML11_TEST_GET_OR_FALLBACK(string, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(string, integer        );
    TOML11_TEST_GET_OR_FALLBACK(string, floating       );
    TOML11_TEST_GET_OR_FALLBACK(string, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(string, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(string, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(string, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(string, array          );
    TOML11_TEST_GET_OR_FALLBACK(string, table          );

    TOML11_TEST_GET_OR_FALLBACK(local_time, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(local_time, integer        );
    TOML11_TEST_GET_OR_FALLBACK(local_time, floating       );
    TOML11_TEST_GET_OR_FALLBACK(local_time, string         );
    TOML11_TEST_GET_OR_FALLBACK(local_time, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(local_time, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(local_time, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(local_time, array          );
    TOML11_TEST_GET_OR_FALLBACK(local_time, table          );

    TOML11_TEST_GET_OR_FALLBACK(local_date, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(local_date, integer        );
    TOML11_TEST_GET_OR_FALLBACK(local_date, floating       );
    TOML11_TEST_GET_OR_FALLBACK(local_date, string         );
    TOML11_TEST_GET_OR_FALLBACK(local_date, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(local_date, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(local_date, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(local_date, array          );
    TOML11_TEST_GET_OR_FALLBACK(local_date, table          );

    TOML11_TEST_GET_OR_FALLBACK(local_datetime, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, integer        );
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, floating       );
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, string         );
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, array          );
    TOML11_TEST_GET_OR_FALLBACK(local_datetime, table          );

    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, integer        );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, floating       );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, string         );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, array          );
    TOML11_TEST_GET_OR_FALLBACK(offset_datetime, table          );

    TOML11_TEST_GET_OR_FALLBACK(array, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(array, integer        );
    TOML11_TEST_GET_OR_FALLBACK(array, floating       );
    TOML11_TEST_GET_OR_FALLBACK(array, string         );
    TOML11_TEST_GET_OR_FALLBACK(array, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(array, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(array, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(array, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(array, table          );

    TOML11_TEST_GET_OR_FALLBACK(table, boolean        );
    TOML11_TEST_GET_OR_FALLBACK(table, integer        );
    TOML11_TEST_GET_OR_FALLBACK(table, floating       );
    TOML11_TEST_GET_OR_FALLBACK(table, string         );
    TOML11_TEST_GET_OR_FALLBACK(table, local_time     );
    TOML11_TEST_GET_OR_FALLBACK(table, local_date     );
    TOML11_TEST_GET_OR_FALLBACK(table, local_datetime );
    TOML11_TEST_GET_OR_FALLBACK(table, offset_datetime);
    TOML11_TEST_GET_OR_FALLBACK(table, array          );
}
#undef TOML11_TEST_GET_OR_FALLBACK

BOOST_AUTO_TEST_CASE(test_get_or_integer)
{
    {
        toml::value v1(42);
        toml::value v2(3.14);
        BOOST_TEST(42u == toml::get_or(v1, 0u));
        BOOST_TEST(0u ==  toml::get_or(v2, 0u));
    }
    {
        toml::value v1(42);
        toml::value v2(3.14);
        BOOST_TEST(42u == toml::get_or(std::move(v1), 0u));
        BOOST_TEST(0u ==  toml::get_or(std::move(v2), 0u));
    }

}

BOOST_AUTO_TEST_CASE(test_get_or_floating)
{
    {
        toml::value v1(42);
        toml::value v2(3.14);
        BOOST_TEST(2.71f == toml::get_or(v1, 2.71f));
        BOOST_TEST(static_cast<float>(v2.as_floating()) == toml::get_or(v2, 2.71f));
    }
    {
        toml::value v1(42);
        toml::value v2(3.14);
        BOOST_TEST(2.71f                    == toml::get_or(std::move(v1), 2.71f));
        BOOST_TEST(static_cast<float>(3.14) == toml::get_or(std::move(v2), 2.71f));
    }
}

BOOST_AUTO_TEST_CASE(test_get_or_string)
{
    {
        toml::value v1("foobar");
        toml::value v2(42);

        std::string       s1("bazqux");
        const std::string s2("bazqux");

        BOOST_TEST("foobar" == toml::get_or(v1, s1));
        BOOST_TEST("bazqux" == toml::get_or(v2, s1));

        std::string& v1r = toml::get_or(v1, s1);
        std::string& s1r = toml::get_or(v2, s1);

        BOOST_TEST("foobar" == v1r);
        BOOST_TEST("bazqux" == s1r);

        BOOST_TEST("foobar" == toml::get_or(v1, s2));
        BOOST_TEST("bazqux" == toml::get_or(v2, s2));

        BOOST_TEST("foobar" == toml::get_or(v1, std::move(s1)));
        BOOST_TEST("bazqux" == toml::get_or(v2, std::move(s1)));
    }
    {
        toml::value v1("foobar");
        toml::value v2(42);

        std::string       s1("bazqux");
        const std::string s2("bazqux");

        BOOST_TEST("foobar" == toml::get_or(std::move(v1), s1));
        BOOST_TEST("bazqux" == toml::get_or(std::move(v2), s1));
    }
    {
        toml::value v1("foobar");
        toml::value v2(42);

        BOOST_TEST("foobar" == toml::get_or(v1, "bazqux"));
        BOOST_TEST("bazqux" == toml::get_or(v2, "bazqux"));

        const char* lit = "bazqux";
        BOOST_TEST("foobar" == toml::get_or(v1, lit));
        BOOST_TEST("bazqux" == toml::get_or(v2, lit));
    }
    {
        toml::value v1("foobar");
        toml::value v2(42);

        BOOST_TEST("foobar" == toml::get_or(std::move(v1), "bazqux"));
        BOOST_TEST("bazqux" == toml::get_or(std::move(v2), "bazqux"));
    }
    {
        toml::value v1("foobar");
        toml::value v2(42);

        const char* lit = "bazqux";
        BOOST_TEST("foobar" == toml::get_or(v1, lit));
        BOOST_TEST("bazqux" == toml::get_or(v2, lit));
    }

}
