#include <toml.hpp>

#include "unit_test.hpp"

BOOST_AUTO_TEST_CASE(test_comment_before)
{
    {
        const std::string file = R"(
            # comment for a.
            a = 42
            # comment for b.
            b = "baz"
        )";
        std::istringstream iss(file);
        const auto v = toml::parse<toml::preserve_comments>(iss);

        const auto& a = toml::find(v, "a");
        const auto& b = toml::find(v, "b");

        BOOST_TEST(a.comments().size()  == 1u);
        BOOST_TEST(a.comments().front() == " comment for a.");
        BOOST_TEST(b.comments().size()  == 1u);
        BOOST_TEST(b.comments().front() == " comment for b.");
    }
    {
        const std::string file = R"(
            # comment for a.
            # another comment for a.
            a = 42
            # comment for b.
            # also comment for b.
            b = "baz"
        )";

        std::istringstream iss(file);
        const auto v = toml::parse<toml::preserve_comments>(iss);

        const auto& a = toml::find(v, "a");
        const auto& b = toml::find(v, "b");

        BOOST_TEST(a.comments().size()  == 2u);
        BOOST_TEST(a.comments().front() == " comment for a.");
        BOOST_TEST(a.comments().back()  == " another comment for a.");
        BOOST_TEST(b.comments().size()  == 2u);
        BOOST_TEST(b.comments().front() == " comment for b.");
        BOOST_TEST(b.comments().back()  == " also comment for b.");
    }
}

BOOST_AUTO_TEST_CASE(test_comment_inline)
{
    {
        const std::string file = R"(
            a = 42    # comment for a.
            b = "baz" # comment for b.
        )";

        std::istringstream iss(file);
        const auto v = toml::parse<toml::preserve_comments>(iss);

        const auto& a = toml::find(v, "a");
        const auto& b = toml::find(v, "b");

        BOOST_TEST(a.comments().size()  == 1u);
        BOOST_TEST(a.comments().front() == " comment for a.");
        BOOST_TEST(b.comments().size()  == 1u);
        BOOST_TEST(b.comments().front() == " comment for b.");
    }
    {
        const std::string file = R"(
            a = [
                42,
            ] # comment for a.
            b = [
                "bar", # this is not a comment for b, but "bar"
            ] # this is a comment for b.
        )";

        std::istringstream iss(file);
        const auto v = toml::parse<toml::preserve_comments>(iss);

        const auto& a  = toml::find(v, "a");
        const auto& b  = toml::find(v, "b");
        const auto& b0 = b.as_array().at(0);

        BOOST_TEST(a.comments().size()   == 1u);
        BOOST_TEST(a.comments().front()  == " comment for a.");
        BOOST_TEST(b.comments().size()   == 1u);
        BOOST_TEST(b.comments().front()  == " this is a comment for b.");
        BOOST_TEST(b0.comments().size()  == 1u);
        BOOST_TEST(b0.comments().front() == " this is not a comment for b, but \"bar\"");
    }
}

BOOST_AUTO_TEST_CASE(test_comment_both)
{
    {
        const std::string file = R"(
            # comment for a.
            a = 42 # inline comment for a.
            # comment for b.
            b = "baz" # inline comment for b.
            # comment for c.
            c = [ # this comment will be ignored
                # comment for the first element.
                10 # this also.
            ] # another comment for c.
        )";

        std::istringstream iss(file);
        const auto v = toml::parse<toml::preserve_comments>(iss);

        const auto& a  = toml::find(v, "a");
        const auto& b  = toml::find(v, "b");
        const auto& c  = toml::find(v, "c");
        const auto& c0 = c.as_array().at(0);

        BOOST_TEST(a.comments().size()  == 2u);
        BOOST_TEST(a.comments().front() == " comment for a.");
        BOOST_TEST(a.comments().back()  == " inline comment for a.");
        BOOST_TEST(b.comments().size()  == 2u);
        BOOST_TEST(b.comments().front() == " comment for b.");
        BOOST_TEST(b.comments().back()  == " inline comment for b.");

        BOOST_TEST(c.comments().size()  == 2u);
        BOOST_TEST(c.comments().front() == " comment for c.");
        BOOST_TEST(c.comments().back()  == " another comment for c.");

        BOOST_TEST(c0.comments().size()  == 2u);
        BOOST_TEST(c0.comments().front() == " comment for the first element.");
        BOOST_TEST(c0.comments().back()  == " this also.");
    }
}

BOOST_AUTO_TEST_CASE(test_comments_on_implicit_values)
{
    {
        const std::string file = R"(
            # comment for the first element of array-of-tables.
            [[array-of-tables]]
            foo = "bar"
        )";
        std::istringstream iss(file);
        const auto v = toml::parse<toml::preserve_comments>(iss);

        const auto aot = toml::find(v, "array-of-tables");
        const auto elm = aot.at(0);
        BOOST_TEST(aot.comments().empty());
        BOOST_TEST(elm.comments().size() == 1);
        BOOST_TEST(elm.comments().front() == " comment for the first element of array-of-tables.");
    }
    {
        const std::string file = R"(
            # comment for the array itself
            array-of-tables = [
                # comment for the first element of array-of-tables.
                {foo = "bar"}
            ]
        )";
        std::istringstream iss(file);
        const auto v = toml::parse<toml::preserve_comments>(iss);

        const auto aot = toml::find(v, "array-of-tables");
        const auto elm = aot.at(0);
        BOOST_TEST(aot.comments().size() == 1);
        BOOST_TEST(aot.comments().front() == " comment for the array itself");
        BOOST_TEST(elm.comments().size() == 1);
        BOOST_TEST(elm.comments().front() == " comment for the first element of array-of-tables.");
    }
}

BOOST_AUTO_TEST_CASE(test_discard_comment)
{
    const std::string file = R"(
        # comment for a.
        a = 42 # inline comment for a.
        # comment for b.
        b = "baz" # inline comment for b.
        # comment for c.
        c = [ # this comment will be ignored
            # comment for the first element.
            10 # this also.
        ] # another comment for c.
    )";

    std::istringstream iss(file);
    const auto v = toml::parse<toml::discard_comments>(iss);

    const auto& a  = toml::find(v, "a");
    const auto& b  = toml::find(v, "b");
    const auto& c  = toml::find(v, "c");
    const auto& c0 = c.as_array().at(0);

    BOOST_TEST(a.comments().empty());
    BOOST_TEST(b.comments().empty());
    BOOST_TEST(c.comments().empty());
    BOOST_TEST(c0.comments().empty());
}

BOOST_AUTO_TEST_CASE(test_construct_value_with_comments)
{
    using value_type = toml::basic_value<toml::preserve_comments>;
    {
        const value_type v(true, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_boolean());
        BOOST_TEST(v.as_boolean() == true);
    }
    {
        const value_type v(42, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_integer());
        BOOST_TEST(v.as_integer() == 42);
    }
    {
        const value_type v(3.14, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_floating());
        BOOST_TEST(v.as_floating() == 3.14);
    }
    {
        const value_type v(toml::string("str"), {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_string());
        BOOST_TEST(v.as_string() == "str");
    }
    {
        const value_type v(std::string("str"), {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_string());
        BOOST_TEST(v.as_string() == "str");
    }
    {
        const value_type v(std::string("str"), toml::string_t::literal,
                           {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_string());
        BOOST_TEST(v.as_string() == "str");
    }
    {
        const value_type v("str", {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_string());
        BOOST_TEST(v.as_string() == "str");
    }
    {
        const value_type v("str", toml::string_t::literal,
                           {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_string());
        BOOST_TEST(v.as_string() == "str");
    }
#if TOML11_CPLUSPLUS_STANDARD_VERSION >= 201703L
    {
        using namespace std::literals::string_view_literals;
        const value_type v("str"sv, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_string());
        BOOST_TEST(v.as_string() == "str");
    }
    {
        using namespace std::literals::string_view_literals;
        const value_type v("str"sv, toml::string_t::literal,
                           {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_string());
        BOOST_TEST(v.as_string() == "str");
    }
#endif
    const toml::local_date      ld{2019, toml::month_t::Apr, 1};
    const toml::local_time      lt{12, 30, 45};
    const toml::local_datetime  ldt{ld, lt};
    const toml::offset_datetime odt{ld, lt, toml::time_offset{9, 0}};
    {
        const value_type v(ld, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_local_date());
        BOOST_TEST(v.as_local_date() == ld);
    }
    {
        const value_type v(lt, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_local_time());
        BOOST_TEST(v.as_local_time() == lt);
    }
    {
        const toml::local_time three_hours{3,0,0};
        const value_type v(std::chrono::hours(3), {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_local_time());
        BOOST_TEST(v.as_local_time() == three_hours);
    }
    {
        const value_type v(ldt, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_local_datetime());
        BOOST_TEST(v.as_local_datetime() == ldt);
    }
    {
        const value_type v(odt, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_offset_datetime());
        BOOST_TEST(v.as_offset_datetime() == odt);
    }
    {
        const auto systp = static_cast<std::chrono::system_clock::time_point>(odt);
        const value_type v(systp, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_offset_datetime());

        // While the conversion, the information about time offset may change.
        const auto systp2 = static_cast<std::chrono::system_clock::time_point>(
                v.as_offset_datetime());
        const bool result = systp == systp2; // because there is no operator<<
        BOOST_TEST(result);
    }
    {
        const typename value_type::array_type a{1,2,3,4,5};
        const value_type v(a, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_array());
        BOOST_TEST(v.as_array().at(0).is_integer());
        BOOST_TEST(v.as_array().at(1).is_integer());
        BOOST_TEST(v.as_array().at(2).is_integer());
        BOOST_TEST(v.as_array().at(3).is_integer());
        BOOST_TEST(v.as_array().at(4).is_integer());
        BOOST_TEST(v.as_array().at(0).as_integer() == 1);
        BOOST_TEST(v.as_array().at(1).as_integer() == 2);
        BOOST_TEST(v.as_array().at(2).as_integer() == 3);
        BOOST_TEST(v.as_array().at(3).as_integer() == 4);
        BOOST_TEST(v.as_array().at(4).as_integer() == 5);
    }
    {
        const std::initializer_list<int> a = {1,2,3,4,5};
        const value_type v(a, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_array());
        BOOST_TEST(v.as_array().at(0).is_integer());
        BOOST_TEST(v.as_array().at(1).is_integer());
        BOOST_TEST(v.as_array().at(2).is_integer());
        BOOST_TEST(v.as_array().at(3).is_integer());
        BOOST_TEST(v.as_array().at(4).is_integer());
        BOOST_TEST(v.as_array().at(0).as_integer() == 1);
        BOOST_TEST(v.as_array().at(1).as_integer() == 2);
        BOOST_TEST(v.as_array().at(2).as_integer() == 3);
        BOOST_TEST(v.as_array().at(3).as_integer() == 4);
        BOOST_TEST(v.as_array().at(4).as_integer() == 5);
    }
    {
        const std::vector<int> a = {1,2,3,4,5};
        const value_type v(a, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_array());
        BOOST_TEST(v.as_array().at(0).is_integer());
        BOOST_TEST(v.as_array().at(1).is_integer());
        BOOST_TEST(v.as_array().at(2).is_integer());
        BOOST_TEST(v.as_array().at(3).is_integer());
        BOOST_TEST(v.as_array().at(4).is_integer());
        BOOST_TEST(v.as_array().at(0).as_integer() == 1);
        BOOST_TEST(v.as_array().at(1).as_integer() == 2);
        BOOST_TEST(v.as_array().at(2).as_integer() == 3);
        BOOST_TEST(v.as_array().at(3).as_integer() == 4);
        BOOST_TEST(v.as_array().at(4).as_integer() == 5);
    }
    {
        const typename value_type::table_type t{
                {"key1", 42}, {"key2", "foobar"}
            };
        const value_type v(t, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_table());
        BOOST_TEST(v.as_table().at("key1").is_integer());
        BOOST_TEST(v.as_table().at("key1").as_integer() == 42);
        BOOST_TEST(v.as_table().at("key2").is_string());
        BOOST_TEST(v.as_table().at("key2").as_string() == "foobar");
    }
    {
        const std::initializer_list<std::pair<std::string, value_type>> t{
                {"key1", 42}, {"key2", "foobar"}
            };
        const value_type v(t, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_table());
        BOOST_TEST(v.as_table().at("key1").is_integer());
        BOOST_TEST(v.as_table().at("key1").as_integer() == 42);
        BOOST_TEST(v.as_table().at("key2").is_string());
        BOOST_TEST(v.as_table().at("key2").as_string() == "foobar");
    }
    {
        const std::map<std::string, value_type> t{
                {"key1", 42}, {"key2", "foobar"}
            };
        const value_type v(t, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_table());
        BOOST_TEST(v.as_table().at("key1").is_integer());
        BOOST_TEST(v.as_table().at("key1").as_integer() == 42);
        BOOST_TEST(v.as_table().at("key2").is_string());
        BOOST_TEST(v.as_table().at("key2").as_string() == "foobar");
    }
}

BOOST_AUTO_TEST_CASE(test_overwrite_comments)
{
    using value_type = toml::basic_value<toml::preserve_comments>;
    {
        const value_type v(42, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_integer());
        BOOST_TEST(v.as_integer() == 42);

        const value_type u(v, {"comment3", "comment4"});
        BOOST_TEST(u.comments().size() == 2u);
        BOOST_TEST(u.comments().at(0)  == "comment3");
        BOOST_TEST(u.comments().at(1)  == "comment4");
        BOOST_TEST(u.is_integer());
        BOOST_TEST(u.as_integer() == 42);
    }
    {
        const value_type v(42, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_integer());
        BOOST_TEST(v.as_integer() == 42);

        const value_type u(v);
        BOOST_TEST(u.comments().size() == 2u);
        BOOST_TEST(u.comments().at(0)  == "comment1");
        BOOST_TEST(u.comments().at(1)  == "comment2");
        BOOST_TEST(u.is_integer());
        BOOST_TEST(u.as_integer() == 42);
    }
    {
        const value_type v(42, {"comment1", "comment2"});
        BOOST_TEST(v.comments().size() == 2u);
        BOOST_TEST(v.comments().at(0)  == "comment1");
        BOOST_TEST(v.comments().at(1)  == "comment2");
        BOOST_TEST(v.is_integer());
        BOOST_TEST(v.as_integer() == 42);

        const value_type u(v, {});
        BOOST_TEST(u.comments().size() == 0u);
        BOOST_TEST(u.is_integer());
        BOOST_TEST(u.as_integer() == 42);
    }
}

BOOST_AUTO_TEST_CASE(test_output_comments)
{
    using value_type = toml::basic_value<toml::preserve_comments>;
    {
        const value_type v(42, {"comment1", "comment2"});
        std::ostringstream oss;
        oss << v.comments();

        std::ostringstream ref;
        ref << "#comment1\n";
        ref << "#comment2\n";

        BOOST_TEST(oss.str() == ref.str());
    }
    {
        const value_type v(42, {"comment1", "comment2"});
        std::ostringstream oss;

        // If v is not a table, toml11 assumes that user is writing something
        // like the following.

        oss << "answer = " << v;

        BOOST_TEST(oss.str() == "answer = 42 #comment1comment2");
    }

    {
        const value_type v(42, {"comment1", "comment2"});
        std::ostringstream oss;

        // If v is not a table, toml11 assumes that user is writing something
        // like the following.

        oss << toml::nocomment << "answer = " << v;

        BOOST_TEST(oss.str() == "answer = 42");
    }

    {
        const value_type v(42, {"comment1", "comment2"});
        std::ostringstream oss;

        // If v is not a table, toml11 assumes that user is writing something
        // like the following.

        oss << toml::nocomment << toml::showcomment << "answer = " << v;

        BOOST_TEST(oss.str() == "answer = 42 #comment1comment2");
    }

}
