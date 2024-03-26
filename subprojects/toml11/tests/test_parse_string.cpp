#include <toml/parser.hpp>

#include "unit_test.hpp"
#include "test_parse_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_string)
{
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"The quick brown fox jumps over the lazy dog\"",
        string("The quick brown fox jumps over the lazy dog", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\'The quick brown fox jumps over the lazy dog\'",
        string("The quick brown fox jumps over the lazy dog", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\"\"The quick brown fox \\\njumps over the lazy dog\"\"\"",
        string("The quick brown fox jumps over the lazy dog", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'''The quick brown fox \njumps over the lazy dog'''",
        string("The quick brown fox \njumps over the lazy dog", string_t::literal));
}

BOOST_AUTO_TEST_CASE(test_string_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"The quick brown fox jumps over the lazy dog\"",
        toml::value("The quick brown fox jumps over the lazy dog", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\'The quick brown fox jumps over the lazy dog\'",
        toml::value("The quick brown fox jumps over the lazy dog", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\"\"The quick brown fox \\\njumps over the lazy dog\"\"\"",
        toml::value("The quick brown fox jumps over the lazy dog", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'''The quick brown fox \njumps over the lazy dog'''",
        toml::value("The quick brown fox \njumps over the lazy dog", string_t::literal));
}


BOOST_AUTO_TEST_CASE(test_basic_string)
{
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"GitHub Cofounder & CEO\\nLikes tater tots and beer.\"",
        string("GitHub Cofounder & CEO\nLikes tater tots and beer.", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"192.168.1.1\"",
        string("192.168.1.1", string_t::basic));

#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\xE4\xB8\xAD\xE5\x9B\xBD\"",
        string("\xE4\xB8\xAD\xE5\x9B\xBD", string_t::basic));
#else
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"中国\"",
        string("中国", string_t::basic));
#endif

    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"You'll hate me after this - #\"",
        string("You'll hate me after this - #", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\" And when \\\"'s are in the along with # \\\"\"",
        string(" And when \"'s are in the along with # \"", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"Here are fifteen apostrophes: '''''''''''''''\"",
        string("Here are fifteen apostrophes: '''''''''''''''", string_t::basic));
}

BOOST_AUTO_TEST_CASE(test_basic_string_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"GitHub Cofounder & CEO\\nLikes tater tots and beer.\"",
        value("GitHub Cofounder & CEO\nLikes tater tots and beer.", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"192.168.1.1\"",
        value("192.168.1.1", string_t::basic));
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\xE4\xB8\xAD\xE5\x9B\xBD\"",
        value("\xE4\xB8\xAD\xE5\x9B\xBD", string_t::basic));
#else
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"中国\"",
        value("中国", string_t::basic));
#endif
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"You'll hate me after this - #\"",
        value("You'll hate me after this - #", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\" And when \\\"'s are in the along with # \\\"\"",
        value(" And when \"'s are in the along with # \"", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"Here are fifteen apostrophes: '''''''''''''''\"",
        value("Here are fifteen apostrophes: '''''''''''''''", string_t::basic));
}

BOOST_AUTO_TEST_CASE(test_ml_basic_string)
{
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\"\"\nThe quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\"\"\"",
        string("The quick brown fox jumps over the lazy dog.", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\"\"\\\n  The quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\\\n  \"\"\"",
        string("The quick brown fox jumps over the lazy dog.", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\"\"Here are two quotation marks: \"\". Simple enough.\"\"\"",
        string("Here are two quotation marks: \"\". Simple enough.", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\"\"Here are three quotation marks: \"\"\\\".\"\"\"",
        string("Here are three quotation marks: \"\"\".", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\"\"Here are fifteen quotation marks: \"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\".\"\"\"",
        string("Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "\"\"\"\"This,\" she said, \"is just a pointless statement.\"\"\"\"",
        string("\"This,\" she said, \"is just a pointless statement.\"", string_t::basic));
}

BOOST_AUTO_TEST_CASE(test_ml_basic_string_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\"\"\nThe quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\"\"\"",
        value("The quick brown fox jumps over the lazy dog.", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\"\"\\\n  The quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\\\n  \"\"\"",
        value("The quick brown fox jumps over the lazy dog.", string_t::basic));

    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\"\"Here are two quotation marks: \"\". Simple enough.\"\"\"",
        value("Here are two quotation marks: \"\". Simple enough.", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\"\"Here are three quotation marks: \"\"\\\".\"\"\"",
        value("Here are three quotation marks: \"\"\".", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\"\"Here are fifteen quotation marks: \"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\".\"\"\"",
        value("Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".", string_t::basic));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "\"\"\"\"This,\" she said, \"is just a pointless statement.\"\"\"\"",
        value("\"This,\" she said, \"is just a pointless statement.\"", string_t::basic));
}

BOOST_AUTO_TEST_CASE(test_literal_string)
{
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'C:\\Users\\nodejs\\templates'",
        string("C:\\Users\\nodejs\\templates", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'\\\\ServerX\\admin$\\system32\\'",
        string("\\\\ServerX\\admin$\\system32\\", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'Tom \"Dubs\" Preston-Werner'",
        string("Tom \"Dubs\" Preston-Werner", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'<\\i\\c*\\s*>'",
        string("<\\i\\c*\\s*>", string_t::literal));
}

BOOST_AUTO_TEST_CASE(test_literal_string_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'C:\\Users\\nodejs\\templates'",
        value("C:\\Users\\nodejs\\templates", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'\\\\ServerX\\admin$\\system32\\'",
        value("\\\\ServerX\\admin$\\system32\\", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'Tom \"Dubs\" Preston-Werner'",
        value("Tom \"Dubs\" Preston-Werner", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'<\\i\\c*\\s*>'",
        value("<\\i\\c*\\s*>", string_t::literal));
}

BOOST_AUTO_TEST_CASE(test_ml_literal_string)
{
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'''I [dw]on't need \\d{2} apples'''",
        string("I [dw]on't need \\d{2} apples", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'''\nThe first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n'''",
        string("The first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "''''That's still pointless', she said.'''",
        string("'That's still pointless', she said.", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "'''Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".'''",
        string("Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".", string_t::literal));
    TOML11_TEST_PARSE_EQUAL(parse_string,
        "''''This,' she said, 'is just a pointless statement.''''",
        string("'This,' she said, 'is just a pointless statement.'", string_t::literal));
}

BOOST_AUTO_TEST_CASE(test_ml_literal_string_value)
{
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'''I [dw]on't need \\d{2} apples'''",
        value("I [dw]on't need \\d{2} apples", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'''\nThe first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n'''",
        value("The first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "''''That's still pointless', she said.'''",
        value("'That's still pointless', she said.", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "'''Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".'''",
        value("Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".", string_t::literal));
    TOML11_TEST_PARSE_EQUAL_VALUE(parse_value<toml::value>,
        "''''This,' she said, 'is just a pointless statement.''''",
        value("'This,' she said, 'is just a pointless statement.'", string_t::literal));
}

BOOST_AUTO_TEST_CASE(test_simple_excape_sequences)
{
    TOML11_TEST_PARSE_EQUAL(parse_string,
            R"("\"\\\b\f\n\r\t")",
            string("\"\\\b\f\n\r\t", string_t::basic));
#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
    TOML11_TEST_PARSE_EQUAL(parse_string,
            R"("\e")",
            string("\x1b", string_t::basic));
#endif
}


BOOST_AUTO_TEST_CASE(test_unicode_escape_sequence)
{
#if defined(_MSC_VER) || defined(__INTEL_COMPILER)
    TOML11_TEST_PARSE_EQUAL(parse_string,
            "\"\\u03B1\\u03B2\\u03B3\"",
            string("\xCE\xB1\xCE\xB2\xCE\xB3", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
            "\"\\U0001D7AA\"",
            string("\xF0\x9D\x9E\xAA", string_t::basic));
#else
    TOML11_TEST_PARSE_EQUAL(parse_string,
            "\"\\u03B1\\u03B2\\u03B3\"",
            string("αβγ", string_t::basic));
    TOML11_TEST_PARSE_EQUAL(parse_string,
            "\"\\U0001D7AA\"",
            string("𝞪", string_t::basic));
#endif
}
