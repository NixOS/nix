#include <toml/lexer.hpp>

#include "unit_test.hpp"
#include "test_lex_aux.hpp"

using namespace toml;
using namespace detail;

BOOST_AUTO_TEST_CASE(test_string)
{
    TOML11_TEST_LEX_ACCEPT(lex_string,
            "\"The quick brown fox jumps over the lazy dog\"",
            "\"The quick brown fox jumps over the lazy dog\"");
    TOML11_TEST_LEX_ACCEPT(lex_string,
            "\'The quick brown fox jumps over the lazy dog\'",
            "\'The quick brown fox jumps over the lazy dog\'");
    TOML11_TEST_LEX_ACCEPT(lex_ml_basic_string,
            "\"\"\"The quick brown fox \\\njumps over the lazy dog\"\"\"",
            "\"\"\"The quick brown fox \\\njumps over the lazy dog\"\"\"");
    TOML11_TEST_LEX_ACCEPT(lex_ml_literal_string,
            "'''The quick brown fox \njumps over the lazy dog'''",
            "'''The quick brown fox \njumps over the lazy dog'''");
}

BOOST_AUTO_TEST_CASE(test_basic_string)
{
    TOML11_TEST_LEX_ACCEPT(lex_string,
            "\"GitHub Cofounder & CEO\\nLikes tater tots and beer.\"",
            "\"GitHub Cofounder & CEO\\nLikes tater tots and beer.\"");
    TOML11_TEST_LEX_ACCEPT(lex_string,
            "\"192.168.1.1\"",
            "\"192.168.1.1\"");

    TOML11_TEST_LEX_ACCEPT(lex_string,
            "\"\xE4\xB8\xAD\xE5\x9B\xBD\"",  // UTF-8 string (means "China" in
            "\"\xE4\xB8\xAD\xE5\x9B\xBD\""); // Chinese characters)

    TOML11_TEST_LEX_ACCEPT(lex_string,
            "\"You'll hate me after this - #\"",
            "\"You'll hate me after this - #\"");
    TOML11_TEST_LEX_ACCEPT(lex_string,
            "\" And when \\\"'s are in the string, along with # \\\"\"",
            "\" And when \\\"'s are in the string, along with # \\\"\"");
}

BOOST_AUTO_TEST_CASE(test_ml_basic_string)
{
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "\"\"\"\nThe quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\"\"\"",
        "\"\"\"\nThe quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\"\"\"");
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "\"\"\"\\\n  The quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\\\n  \"\"\"",
        "\"\"\"\\\n  The quick brown \\\n\n  fox jumps over \\\n  the lazy dog.\\\n  \"\"\"");

    TOML11_TEST_LEX_ACCEPT(lex_string,
        "\"\"\"Here are two quotation marks: \"\". Simple enough.\"\"\"",
        "\"\"\"Here are two quotation marks: \"\". Simple enough.\"\"\"");

    TOML11_TEST_LEX_ACCEPT(lex_string,
        "\"\"\"Here are three quotation marks: \"\"\\\".\"\"\"",
        "\"\"\"Here are three quotation marks: \"\"\\\".\"\"\"");

    TOML11_TEST_LEX_ACCEPT(lex_string,
        "\"\"\"Here are fifteen quotation marks: \"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\".\"\"\"",
        "\"\"\"Here are fifteen quotation marks: \"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\"\"\"\\\".\"\"\"");

    TOML11_TEST_LEX_ACCEPT(lex_string,
        "\"\"\"\"This,\" she said, \"is just a pointless statement.\"\"\"\"",
        "\"\"\"\"This,\" she said, \"is just a pointless statement.\"\"\"\"");
}

BOOST_AUTO_TEST_CASE(test_literal_string)
{
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "'C:\\Users\\nodejs\\templates'",
        "'C:\\Users\\nodejs\\templates'");
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "'\\\\ServerX\\admin$\\system32\\'",
        "'\\\\ServerX\\admin$\\system32\\'");
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "'Tom \"Dubs\" Preston-Werner'",
        "'Tom \"Dubs\" Preston-Werner'");
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "'<\\i\\c*\\s*>'",
        "'<\\i\\c*\\s*>'");
}

BOOST_AUTO_TEST_CASE(test_ml_literal_string)
{
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "'''I [dw]on't need \\d{2} apples'''",
        "'''I [dw]on't need \\d{2} apples'''");
    TOML11_TEST_LEX_ACCEPT(lex_string,
        "'''\nThe first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n'''",
        "'''\nThe first newline is\ntrimmed in raw strings.\n   All other whitespace\n   is preserved.\n'''");

    TOML11_TEST_LEX_ACCEPT(lex_string,
        "''''That's still pointless', she said.'''",
        "''''That's still pointless', she said.'''");

    TOML11_TEST_LEX_ACCEPT(lex_string,
        "'''Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".'''",
        "'''Here are fifteen quotation marks: \"\"\"\"\"\"\"\"\"\"\"\"\"\"\".'''");

    TOML11_TEST_LEX_ACCEPT(lex_string,
        "''''This,' she said, 'is just a pointless statement.''''",
        "''''This,' she said, 'is just a pointless statement.''''");
}
