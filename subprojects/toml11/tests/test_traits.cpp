#include <toml/types.hpp>

#include "unit_test.hpp"

#include <array>
#include <deque>
#include <forward_list>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct dummy_type{};

template<typename T>
struct dummy_container
{
    typedef T value_type;
    typedef value_type* pointer;
    typedef value_type& reference;
    typedef value_type const* const_pointer;
    typedef value_type const& const_reference;
    typedef pointer       iterator;
    typedef const_pointer const_iterator;
};

typedef std::array<dummy_type, 10> std_array_type;
typedef std::map<std::string, dummy_type> std_map_type;
typedef std::unordered_map<std::string, dummy_type> std_unordered_map_type;

BOOST_AUTO_TEST_CASE(test_has_xxx)
{
    BOOST_TEST(toml::detail::has_iterator<std::list<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_iterator<std::forward_list<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_iterator<std::deque<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_iterator<std::vector<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_iterator<std::set<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_iterator<std::unordered_set<std::string>>::value);
    BOOST_TEST(toml::detail::has_iterator<std_array_type>::value);
    BOOST_TEST(toml::detail::has_iterator<std_map_type>::value);
    BOOST_TEST(toml::detail::has_iterator<std_unordered_map_type>::value);
    BOOST_TEST(toml::detail::has_iterator<dummy_container<dummy_type>>::value);

    BOOST_TEST(toml::detail::has_value_type<std::list<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_value_type<std::forward_list<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_value_type<std::deque<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_value_type<std::vector<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_value_type<std_array_type>::value);
    BOOST_TEST(toml::detail::has_value_type<std::set<dummy_type>>::value);
    BOOST_TEST(toml::detail::has_value_type<std::unordered_set<std::string>>::value);
    BOOST_TEST(toml::detail::has_value_type<std_map_type>::value);
    BOOST_TEST(toml::detail::has_value_type<std_unordered_map_type>::value);
    BOOST_TEST(toml::detail::has_value_type<dummy_container<dummy_type>>::value);

    BOOST_TEST(toml::detail::has_key_type<std_map_type>::value);
    BOOST_TEST(toml::detail::has_key_type<std_unordered_map_type>::value);
    BOOST_TEST(toml::detail::has_mapped_type<std_map_type>::value);
    BOOST_TEST(toml::detail::has_mapped_type<std_unordered_map_type>::value);
}

BOOST_AUTO_TEST_CASE(test_is_xxx)
{
    BOOST_TEST(toml::detail::is_container<std::list<dummy_type>>::value);
    BOOST_TEST(toml::detail::is_container<std::forward_list<dummy_type>>::value);
    BOOST_TEST(toml::detail::is_container<std::deque<dummy_type>>::value);
    BOOST_TEST(toml::detail::is_container<std::vector<dummy_type>>::value);
    BOOST_TEST(toml::detail::is_container<std_array_type>::value);
    BOOST_TEST(toml::detail::is_container<std::set<dummy_type>>::value);
    BOOST_TEST(toml::detail::is_container<std::unordered_set<std::string>>::value);
    BOOST_TEST(toml::detail::is_container<dummy_container<dummy_type>>::value);

    BOOST_TEST(!toml::detail::is_container<std_map_type>::value);
    BOOST_TEST(!toml::detail::is_container<std_unordered_map_type>::value);

    BOOST_TEST(toml::detail::is_map<std_map_type>::value);
    BOOST_TEST(toml::detail::is_map<std_unordered_map_type>::value);
}
