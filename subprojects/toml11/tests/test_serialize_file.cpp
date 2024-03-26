#include <toml.hpp>

#include "unit_test.hpp"

#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

#include <clocale>

template<typename Comment,
         template<typename ...> class Table,
         template<typename ...> class Array>
bool has_comment_inside(const toml::basic_value<Comment, Table, Array>& v)
{
    if(!v.comments().empty())
    {
        return false;
    }
    // v itself does not have a comment.
    if(v.is_array())
    {
        for(const auto& x : v.as_array())
        {
            if(has_comment_inside(x))
            {
                return false;
            }
        }
    }
    if(v.is_table())
    {
        for(const auto& x : v.as_table())
        {
            if(has_comment_inside(x.second))
            {
                return false;
            }
        }
    }
    return true;
}

BOOST_AUTO_TEST_CASE(test_example)
{
    const auto data = toml::parse(testinput("example.toml"));
    {
        std::ofstream ofs("tmp1.toml");
        ofs << std::setw(80) << data;
    }

    auto serialized = toml::parse("tmp1.toml");
    {
        auto& owner = toml::find(serialized, "owner");
        auto& bio   = toml::find<std::string>(owner, "bio");
        const auto CR = std::find(bio.begin(), bio.end(), '\r');
        if(CR != bio.end())
        {
            bio.erase(CR);
        }
    }
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_example_map_dq)
{
    const auto data = toml::parse<toml::discard_comments, std::map, std::deque>(
            testinput("example.toml"));
    {
        std::ofstream ofs("tmp1_map_dq.toml");
        ofs << std::setw(80) << data;
    }

    auto serialized = toml::parse<toml::discard_comments, std::map, std::deque>(
            "tmp1_map_dq.toml");
    {
        auto& owner = toml::find(serialized, "owner");
        auto& bio   = toml::find<std::string>(owner, "bio");
        const auto CR = std::find(bio.begin(), bio.end(), '\r');
        if(CR != bio.end())
        {
            bio.erase(CR);
        }
    }
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_example_with_comment)
{
    const auto data = toml::parse<toml::preserve_comments>(testinput("example.toml"));
    {
        std::ofstream ofs("tmp1_com.toml");
        ofs << std::setw(80) << data;
    }

    auto serialized = toml::parse<toml::preserve_comments>("tmp1_com.toml");
    {
        auto& owner = toml::find(serialized, "owner");
        auto& bio   = toml::find<std::string>(owner, "bio");
        const auto CR = std::find(bio.begin(), bio.end(), '\r');
        if(CR != bio.end())
        {
            bio.erase(CR);
        }
    }
    BOOST_TEST(data == serialized);
    {
        std::ofstream ofs("tmp1_com1.toml");
        ofs << std::setw(80) << serialized;
    }
}

BOOST_AUTO_TEST_CASE(test_example_with_comment_nocomment)
{
    {
        const auto data = toml::parse<toml::preserve_comments>(testinput("example.toml"));
        {
            std::ofstream ofs("tmp1_com_nocomment.toml");
            ofs << std::setw(80) << toml::nocomment << data;
        }
        const auto serialized = toml::parse<toml::preserve_comments>("tmp1_com_nocomment.toml");
        // check no comment exist
        BOOST_TEST(!has_comment_inside(serialized));
    }
    {
        const auto data_nocomment = toml::parse<toml::discard_comments>(testinput("example.toml"));
        auto serialized = toml::parse<toml::discard_comments>("tmp1_com_nocomment.toml");
        {
            auto& owner = toml::find(serialized, "owner");
            auto& bio   = toml::find<std::string>(owner, "bio");
            const auto CR = std::find(bio.begin(), bio.end(), '\r');
            if(CR != bio.end())
            {
                bio.erase(CR);
            }
        }
        // check collectly serialized
        BOOST_TEST(data_nocomment == serialized);
    }
}

BOOST_AUTO_TEST_CASE(test_example_with_comment_map_dq)
{
    const auto data = toml::parse<toml::preserve_comments, std::map, std::deque>(
            testinput("example.toml"));
    {
        std::ofstream ofs("tmp1_com_map_dq.toml");
        ofs << std::setw(80) << data;
    }

    auto serialized = toml::parse<toml::preserve_comments, std::map, std::deque>(
            "tmp1_com_map_dq.toml");
    {
        auto& owner = toml::find(serialized, "owner");
        auto& bio   = toml::find<std::string>(owner, "bio");
        const auto CR = std::find(bio.begin(), bio.end(), '\r');
        if(CR != bio.end())
        {
            bio.erase(CR);
        }
    }
    BOOST_TEST(data == serialized);
    {
        std::ofstream ofs("tmp1_com1_map_dq.toml");
        ofs << std::setw(80) << serialized;
    }
}

BOOST_AUTO_TEST_CASE(test_example_with_comment_map_dq_nocomment)
{
    {
        const auto data = toml::parse<toml::preserve_comments, std::map, std::deque>(testinput("example.toml"));
        {
            std::ofstream ofs("tmp1_com_map_dq_nocomment.toml");
            ofs << std::setw(80) << toml::nocomment << data;
        }
        const auto serialized = toml::parse<toml::preserve_comments, std::map, std::deque>("tmp1_com_map_dq_nocomment.toml");
        BOOST_TEST(!has_comment_inside(serialized));
    }
    {
        const auto data_nocomment = toml::parse<toml::discard_comments>(testinput("example.toml"));
        auto serialized = toml::parse<toml::discard_comments>("tmp1_com_map_dq_nocomment.toml");
        {
            auto& owner = toml::find(serialized, "owner");
            auto& bio   = toml::find<std::string>(owner, "bio");
            const auto CR = std::find(bio.begin(), bio.end(), '\r');
            if(CR != bio.end())
            {
                bio.erase(CR);
            }
        }
        BOOST_TEST(data_nocomment == serialized);
    }
}

BOOST_AUTO_TEST_CASE(test_fruit)
{
    const auto data = toml::parse(testinput("fruit.toml"));
    {
        std::ofstream ofs("tmp2.toml");
        ofs << std::setw(80) << data;
    }
    const auto serialized = toml::parse("tmp2.toml");
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_fruit_map_dq)
{
    const auto data = toml::parse<toml::discard_comments, std::map, std::deque>(
            testinput("fruit.toml"));
    {
        std::ofstream ofs("tmp2.toml");
        ofs << std::setw(80) << data;
    }
    const auto serialized = toml::parse<toml::discard_comments, std::map, std::deque>(
            "tmp2.toml");
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_fruit_with_comments)
{
    const auto data = toml::parse<toml::preserve_comments>(testinput("fruit.toml"));
    {
        std::ofstream ofs("tmp2_com.toml");
        ofs << std::setw(80) << data;
    }
    const auto serialized = toml::parse<toml::preserve_comments>("tmp2_com.toml");
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_fruit_with_comments_map_dq)
{
    const auto data = toml::parse<toml::preserve_comments, std::map, std::deque>(
            testinput("fruit.toml"));
    {
        std::ofstream ofs("tmp2_com.toml");
        ofs << std::setw(80) << data;
    }
    const auto serialized = toml::parse<toml::preserve_comments, std::map, std::deque>("tmp2_com.toml");
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_hard_example)
{
    const auto data = toml::parse(testinput("hard_example.toml"));
    {
        std::ofstream ofs("tmp3.toml");
        ofs << std::setw(80) << data;
    }
    const auto serialized = toml::parse("tmp3.toml");
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_hard_example_map_dq)
{
    const auto data = toml::parse<toml::discard_comments, std::map, std::deque>(
            testinput("hard_example.toml"));
    {
        std::ofstream ofs("tmp3.toml");
        ofs << std::setw(80) << data;
    }
    const auto serialized = toml::parse<toml::discard_comments, std::map, std::deque>(
            "tmp3.toml");
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_hard_example_with_comment)
{
    const auto data = toml::parse<toml::preserve_comments, std::map, std::deque>(
            testinput("hard_example.toml"));
    {
        std::ofstream ofs("tmp3_com.toml");
        ofs << std::setw(80) << data;
    }
    const auto serialized = toml::parse<toml::preserve_comments, std::map, std::deque>(
            "tmp3_com.toml");
    {
        std::ofstream ofs("tmp3_com1.toml");
        ofs << std::setw(80) << serialized;
    }
    BOOST_TEST(data == serialized);
}

BOOST_AUTO_TEST_CASE(test_format_key)
{
    {
        const toml::key key("normal_bare-key");
        BOOST_TEST("normal_bare-key" == toml::format_key(key));
    }
    {
        const toml::key key("key.include.dots");
        BOOST_TEST("\"key.include.dots\"" == toml::format_key(key));
    }
    {
        const toml::key key("key-include-unicode-\xE3\x81\x82");
        BOOST_TEST("\"key-include-unicode-\xE3\x81\x82\"" == toml::format_key(key));
    }
    {
        const toml::key key("special-chars-\\-\"-\b-\f-\r-\n-\t");
        BOOST_TEST("\"special-chars-\\\\-\\\"-\\b-\\f-\\r-\\n-\\t\"" == toml::format_key(key));
    }
}

// In toml11, an implicitly-defined value does not have any comments.
// So, in the following file,
// ```toml
// # comment
// [[array-of-tables]]
// foo = "bar"
// ```
// The array named "array-of-tables" does not have the comment, but the first
// element of the array has. That means that, the above file is equivalent to
// the following.
// ```toml
// array-of-tables = [
//     # comment
//     {foo = "bar"},
// ]
// ```
// If the array itself has a comment (value_has_comment_ == true), we should try
// to make it inline.
// ```toml
// # comment about array
// array-of-tables = [
//   # comment about table element
//   {foo = "bar"}
// ]
// ```
// If it is formatted as a multiline table, the two comments becomes
// indistinguishable.
// ```toml
// # comment about array
// # comment about table element
// [[array-of-tables]]
// foo = "bar"
// ```
// So we need to try to make it inline, and it force-inlines regardless
// of the line width limit.
//     It may fail if the element of a table has comment. In that case,
// the array-of-tables will be formatted as a multiline table.
BOOST_AUTO_TEST_CASE(test_distinguish_comment)
{
    const std::string str = R"(# comment about array itself
array_of_table = [
    # comment about the first element (table)
    {key = "value"},
])";
    std::istringstream iss(str);
    const auto data = toml::parse<toml::preserve_comments>(iss);
    const auto serialized = toml::format(data, /*width = */ 0);

    std::istringstream reparse(serialized);
    const auto parsed = toml::parse<toml::preserve_comments>(reparse);

    BOOST_TEST(parsed.at("array_of_table").comments().size()  == 1u);
    BOOST_TEST(parsed.at("array_of_table").comments().front() == " comment about array itself");
    BOOST_TEST(parsed.at("array_of_table").at(0).comments().size()  == 1u);
    BOOST_TEST(parsed.at("array_of_table").at(0).comments().front() == " comment about the first element (table)");
}


BOOST_AUTO_TEST_CASE(test_serialize_under_locale)
{
    // avoid null init (setlocale returns null when it failed)
    std::string setloc(std::setlocale(LC_ALL, nullptr));

    // fr_FR is a one of locales that uses `,` as a decimal separator.
    if(const char* try_hyphen = std::setlocale(LC_ALL, "fr_FR.UTF-8"))
    {
        setloc = std::string(try_hyphen);
    }
    else if(const char* try_nohyphen = std::setlocale(LC_ALL, "fr_FR.utf8"))
    {
        setloc = std::string(try_nohyphen);
    }
    // In some envs, fr_FR locale has not been installed. Tests must work even in such a case.
//     else
//     {
//         BOOST_TEST(false);
//     }
    BOOST_TEST_MESSAGE("current locale at the beginning of the test = " << setloc);

    const std::string str = R"(
pi        = 3.14159
large_int = 1234567890
)";
    std::istringstream iss(str);
    const auto ref = toml::parse(iss);
    const auto serialized_str = toml::format(ref, /*width = */ 80);

    BOOST_TEST_MESSAGE("serialized = " << serialized_str);

    std::istringstream serialized_iss(serialized_str);
    const auto serialized_ref = toml::parse(serialized_iss);

    BOOST_TEST(serialized_ref.at("pi").as_floating() == ref.at("pi").as_floating());
    BOOST_TEST(serialized_ref.at("large_int").as_integer() == ref.at("large_int").as_integer());

    const std::string endloc(std::setlocale(LC_ALL, nullptr));
    BOOST_TEST_MESSAGE("current locale at the end of the test = " << endloc);
    // check if serializer change global locale
    BOOST_TEST(setloc == endloc);
}
