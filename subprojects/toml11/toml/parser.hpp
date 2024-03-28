//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_PARSER_HPP
#define TOML11_PARSER_HPP
#include <cstring>
#include <fstream>
#include <sstream>

#include "combinator.hpp"
#include "lexer.hpp"
#include "macros.hpp"
#include "region.hpp"
#include "result.hpp"
#include "types.hpp"
#include "value.hpp"

#ifndef TOML11_DISABLE_STD_FILESYSTEM
#ifdef __cpp_lib_filesystem
#if __has_include(<filesystem>)
#define TOML11_HAS_STD_FILESYSTEM
#include <filesystem>
#endif // has_include(<string_view>)
#endif // __cpp_lib_filesystem
#endif // TOML11_DISABLE_STD_FILESYSTEM

// the previous commit works with 500+ recursions. so it may be too small.
// but in most cases, i think we don't need such a deep recursion of
// arrays or inline-tables.
#define TOML11_VALUE_RECURSION_LIMIT 64

namespace toml
{
namespace detail
{

inline result<std::pair<boolean, region>, std::string>
parse_boolean(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_boolean::invoke(loc))
    {
        const auto reg = token.unwrap();
        if     (reg.str() == "true")  {return ok(std::make_pair(true,  reg));}
        else if(reg.str() == "false") {return ok(std::make_pair(false, reg));}
        else // internal error.
        {
            throw internal_error(format_underline(
                "toml::parse_boolean: internal error",
                {{source_location(reg), "invalid token"}}),
                source_location(reg));
        }
    }
    loc.reset(first); //rollback
    return err(format_underline("toml::parse_boolean: ",
               {{source_location(loc), "the next token is not a boolean"}}));
}

inline result<std::pair<integer, region>, std::string>
parse_binary_integer(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_bin_int::invoke(loc))
    {
        auto str = token.unwrap().str();
        assert(str.size() > 2); // minimum -> 0b1
        assert(str.at(0) == '0' && str.at(1) == 'b');

        // skip all the zeros and `_` locating at the MSB
        str.erase(str.begin(), std::find_if(
                str.begin() + 2, // to skip prefix `0b`
                str.end(),
                [](const char c) { return c == '1'; })
            );
        assert(str.empty() || str.front() == '1');

        // since toml11 uses int64_t, 64bit (unsigned) input cannot be read.
        const auto max_length = 63 + std::count(str.begin(), str.end(), '_');
        if(static_cast<std::string::size_type>(max_length) < str.size())
        {
            loc.reset(first);
            return err(format_underline("toml::parse_binary_integer: "
                "only signed 64bit integer is available",
               {{source_location(loc), "too large input (> int64_t)"}}));
        }

        integer retval(0), base(1);
        for(auto i(str.rbegin()), e(str.rend()); i!=e; ++i)
        {
            assert(base != 0); // means overflow, checked in the above code
            if(*i == '1')
            {
                retval += base;
                if( (std::numeric_limits<integer>::max)() / 2 < base )
                {
                    base = 0;
                }
                base *= 2;
            }
            else if(*i == '0')
            {
                if( (std::numeric_limits<integer>::max)() / 2 < base )
                {
                    base = 0;
                }
                base *= 2;
            }
            else if(*i == '_')
            {
                // do nothing.
            }
            else // should be detected by lex_bin_int. [[unlikely]]
            {
                throw internal_error(format_underline(
                    "toml::parse_binary_integer: internal error",
                    {{source_location(token.unwrap()), "invalid token"}}),
                    source_location(loc));
            }
        }
        return ok(std::make_pair(retval, token.unwrap()));
    }
    loc.reset(first);
    return err(format_underline("toml::parse_binary_integer:",
               {{source_location(loc), "the next token is not an integer"}}));
}

inline result<std::pair<integer, region>, std::string>
parse_octal_integer(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_oct_int::invoke(loc))
    {
        auto str = token.unwrap().str();
        str.erase(std::remove(str.begin(), str.end(), '_'), str.end());
        str.erase(str.begin()); str.erase(str.begin()); // remove `0o` prefix

        std::istringstream iss(str);
        integer retval(0);
        iss >> std::oct >> retval;
        if(iss.fail())
        {
            // `istream` sets `failbit` if internally-called `std::num_get::get`
            // fails.
            // `std::num_get::get` calls `std::strtoll` if the argument type is
            // signed.
            // `std::strtoll` fails if
            //  - the value is out_of_range or
            //  - no conversion is possible.
            // since we already checked that the string is valid octal integer,
            // so the error reason is out_of_range.
            loc.reset(first);
            return err(format_underline("toml::parse_octal_integer:",
                       {{source_location(loc), "out of range"}}));
        }
        return ok(std::make_pair(retval, token.unwrap()));
    }
    loc.reset(first);
    return err(format_underline("toml::parse_octal_integer:",
               {{source_location(loc), "the next token is not an integer"}}));
}

inline result<std::pair<integer, region>, std::string>
parse_hexadecimal_integer(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_hex_int::invoke(loc))
    {
        auto str = token.unwrap().str();
        str.erase(std::remove(str.begin(), str.end(), '_'), str.end());
        str.erase(str.begin()); str.erase(str.begin()); // remove `0x` prefix

        std::istringstream iss(str);
        integer retval(0);
        iss >> std::hex >> retval;
        if(iss.fail())
        {
            // see parse_octal_integer for detail of this error message.
            loc.reset(first);
            return err(format_underline("toml::parse_hexadecimal_integer:",
                       {{source_location(loc), "out of range"}}));
        }
        return ok(std::make_pair(retval, token.unwrap()));
    }
    loc.reset(first);
    return err(format_underline("toml::parse_hexadecimal_integer",
               {{source_location(loc), "the next token is not an integer"}}));
}

inline result<std::pair<integer, region>, std::string>
parse_integer(location& loc)
{
    const auto first = loc.iter();
    if(first != loc.end() && *first == '0')
    {
        const auto second = std::next(first);
        if(second == loc.end()) // the token is just zero.
        {
            loc.advance();
            return ok(std::make_pair(0, region(loc, first, second)));
        }

        if(*second == 'b') {return parse_binary_integer     (loc);} // 0b1100
        if(*second == 'o') {return parse_octal_integer      (loc);} // 0o775
        if(*second == 'x') {return parse_hexadecimal_integer(loc);} // 0xC0FFEE

        if(std::isdigit(*second))
        {
            return err(format_underline("toml::parse_integer: "
                "leading zero in an Integer is not allowed.",
                {{source_location(loc), "leading zero"}}));
        }
        else if(std::isalpha(*second))
        {
             return err(format_underline("toml::parse_integer: "
                "unknown integer prefix appeared.",
                {{source_location(loc), "none of 0x, 0o, 0b"}}));
        }
    }

    if(const auto token = lex_dec_int::invoke(loc))
    {
        auto str = token.unwrap().str();
        str.erase(std::remove(str.begin(), str.end(), '_'), str.end());

        std::istringstream iss(str);
        integer retval(0);
        iss >> retval;
        if(iss.fail())
        {
            // see parse_octal_integer for detail of this error message.
            loc.reset(first);
            return err(format_underline("toml::parse_integer:",
                       {{source_location(loc), "out of range"}}));
        }
        return ok(std::make_pair(retval, token.unwrap()));
    }
    loc.reset(first);
    return err(format_underline("toml::parse_integer: ",
               {{source_location(loc), "the next token is not an integer"}}));
}

inline result<std::pair<floating, region>, std::string>
parse_floating(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_float::invoke(loc))
    {
        auto str = token.unwrap().str();
        if(str == "inf" || str == "+inf")
        {
            if(std::numeric_limits<floating>::has_infinity)
            {
                return ok(std::make_pair(
                    std::numeric_limits<floating>::infinity(), token.unwrap()));
            }
            else
            {
                throw std::domain_error("toml::parse_floating: inf value found"
                    " but the current environment does not support inf. Please"
                    " make sure that the floating-point implementation conforms"
                    " IEEE 754/ISO 60559 international standard.");
            }
        }
        else if(str == "-inf")
        {
            if(std::numeric_limits<floating>::has_infinity)
            {
                return ok(std::make_pair(
                    -std::numeric_limits<floating>::infinity(), token.unwrap()));
            }
            else
            {
                throw std::domain_error("toml::parse_floating: inf value found"
                    " but the current environment does not support inf. Please"
                    " make sure that the floating-point implementation conforms"
                    " IEEE 754/ISO 60559 international standard.");
            }
        }
        else if(str == "nan" || str == "+nan")
        {
            if(std::numeric_limits<floating>::has_quiet_NaN)
            {
                return ok(std::make_pair(
                    std::numeric_limits<floating>::quiet_NaN(), token.unwrap()));
            }
            else if(std::numeric_limits<floating>::has_signaling_NaN)
            {
                return ok(std::make_pair(
                    std::numeric_limits<floating>::signaling_NaN(), token.unwrap()));
            }
            else
            {
                throw std::domain_error("toml::parse_floating: NaN value found"
                    " but the current environment does not support NaN. Please"
                    " make sure that the floating-point implementation conforms"
                    " IEEE 754/ISO 60559 international standard.");
            }
        }
        else if(str == "-nan")
        {
            if(std::numeric_limits<floating>::has_quiet_NaN)
            {
                return ok(std::make_pair(
                    -std::numeric_limits<floating>::quiet_NaN(), token.unwrap()));
            }
            else if(std::numeric_limits<floating>::has_signaling_NaN)
            {
                return ok(std::make_pair(
                    -std::numeric_limits<floating>::signaling_NaN(), token.unwrap()));
            }
            else
            {
                throw std::domain_error("toml::parse_floating: NaN value found"
                    " but the current environment does not support NaN. Please"
                    " make sure that the floating-point implementation conforms"
                    " IEEE 754/ISO 60559 international standard.");
            }
        }
        str.erase(std::remove(str.begin(), str.end(), '_'), str.end());
        std::istringstream iss(str);
        floating v(0.0);
        iss >> v;
        if(iss.fail())
        {
            // see parse_octal_integer for detail of this error message.
            loc.reset(first);
            return err(format_underline("toml::parse_floating:",
                       {{source_location(loc), "out of range"}}));
        }
        return ok(std::make_pair(v, token.unwrap()));
    }
    loc.reset(first);
    return err(format_underline("toml::parse_floating: ",
               {{source_location(loc), "the next token is not a float"}}));
}

inline std::string read_utf8_codepoint(const region& reg, const location& loc)
{
    const auto str = reg.str().substr(1);
    std::uint_least32_t codepoint;
    std::istringstream iss(str);
    iss >> std::hex >> codepoint;

    const auto to_char = [](const std::uint_least32_t i) noexcept -> char {
        const auto uc = static_cast<unsigned char>(i);
        return *reinterpret_cast<const char*>(std::addressof(uc));
    };

    std::string character;
    if(codepoint < 0x80) // U+0000 ... U+0079 ; just an ASCII.
    {
        character += static_cast<char>(codepoint);
    }
    else if(codepoint < 0x800) //U+0080 ... U+07FF
    {
        // 110yyyyx 10xxxxxx; 0x3f == 0b0011'1111
        character += to_char(0xC0| codepoint >> 6);
        character += to_char(0x80|(codepoint & 0x3F));
    }
    else if(codepoint < 0x10000) // U+0800...U+FFFF
    {
        if(0xD800 <= codepoint && codepoint <= 0xDFFF)
        {
            throw syntax_error(format_underline(
                "toml::read_utf8_codepoint: codepoints in the range "
                "[0xD800, 0xDFFF] are not valid UTF-8.", {{
                    source_location(loc), "not a valid UTF-8 codepoint"
                }}), source_location(loc));
        }
        assert(codepoint < 0xD800 || 0xDFFF < codepoint);
        // 1110yyyy 10yxxxxx 10xxxxxx
        character += to_char(0xE0| codepoint >> 12);
        character += to_char(0x80|(codepoint >> 6 & 0x3F));
        character += to_char(0x80|(codepoint      & 0x3F));
    }
    else if(codepoint < 0x110000) // U+010000 ... U+10FFFF
    {
        // 11110yyy 10yyxxxx 10xxxxxx 10xxxxxx
        character += to_char(0xF0| codepoint >> 18);
        character += to_char(0x80|(codepoint >> 12 & 0x3F));
        character += to_char(0x80|(codepoint >> 6  & 0x3F));
        character += to_char(0x80|(codepoint       & 0x3F));
    }
    else // out of UTF-8 region
    {
        throw syntax_error(format_underline("toml::read_utf8_codepoint:"
            " input codepoint is too large.",
            {{source_location(loc), "should be in [0x00..0x10FFFF]"}}),
            source_location(loc));
    }
    return character;
}

inline result<std::string, std::string> parse_escape_sequence(location& loc)
{
    const auto first = loc.iter();
    if(first == loc.end() || *first != '\\')
    {
        return err(format_underline("toml::parse_escape_sequence: ", {{
            source_location(loc), "the next token is not a backslash \"\\\""}}));
    }
    loc.advance();
    switch(*loc.iter())
    {
        case '\\':{loc.advance(); return ok(std::string("\\"));}
        case '"' :{loc.advance(); return ok(std::string("\""));}
        case 'b' :{loc.advance(); return ok(std::string("\b"));}
        case 't' :{loc.advance(); return ok(std::string("\t"));}
        case 'n' :{loc.advance(); return ok(std::string("\n"));}
        case 'f' :{loc.advance(); return ok(std::string("\f"));}
        case 'r' :{loc.advance(); return ok(std::string("\r"));}
#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
        case 'e' :{loc.advance(); return ok(std::string("\x1b"));} // ESC
#endif
        case 'u' :
        {
            if(const auto token = lex_escape_unicode_short::invoke(loc))
            {
                return ok(read_utf8_codepoint(token.unwrap(), loc));
            }
            else
            {
                return err(format_underline("parse_escape_sequence: "
                           "invalid token found in UTF-8 codepoint uXXXX.",
                           {{source_location(loc), "here"}}));
            }
        }
        case 'U':
        {
            if(const auto token = lex_escape_unicode_long::invoke(loc))
            {
                return ok(read_utf8_codepoint(token.unwrap(), loc));
            }
            else
            {
                return err(format_underline("parse_escape_sequence: "
                           "invalid token found in UTF-8 codepoint Uxxxxxxxx",
                           {{source_location(loc), "here"}}));
            }
        }
    }

    const auto msg = format_underline("parse_escape_sequence: "
           "unknown escape sequence appeared.", {{source_location(loc),
           "escape sequence is one of \\, \", b, t, n, f, r, uxxxx, Uxxxxxxxx"}},
           /* Hints = */{"if you want to write backslash as just one backslash, "
           "use literal string like: regex    = '<\\i\\c*\\s*>'"});
    loc.reset(first);
    return err(msg);
}

inline std::ptrdiff_t check_utf8_validity(const std::string& reg)
{
    location loc("tmp", reg);
    const auto u8 = repeat<lex_utf8_code, unlimited>::invoke(loc);
    if(!u8 || loc.iter() != loc.end())
    {
        const auto error_location = std::distance(loc.begin(), loc.iter());
        assert(0 <= error_location);
        return error_location;
    }
    return -1;
}

inline result<std::pair<toml::string, region>, std::string>
parse_ml_basic_string(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_ml_basic_string::invoke(loc))
    {
        auto inner_loc = loc;
        inner_loc.reset(first);

        std::string retval;
        retval.reserve(token.unwrap().size());

        auto delim = lex_ml_basic_string_open::invoke(inner_loc);
        if(!delim)
        {
            throw internal_error(format_underline(
                "parse_ml_basic_string: invalid token",
                {{source_location(inner_loc), "should be \"\"\""}}),
                source_location(inner_loc));
        }
        // immediate newline is ignored (if exists)
        /* discard return value */ lex_newline::invoke(inner_loc);

        delim = none();
        while(!delim)
        {
            using lex_unescaped_seq = repeat<
                either<lex_ml_basic_unescaped, lex_newline>, unlimited>;
            if(auto unescaped = lex_unescaped_seq::invoke(inner_loc))
            {
                retval += unescaped.unwrap().str();
            }
            if(auto escaped = parse_escape_sequence(inner_loc))
            {
                retval += escaped.unwrap();
            }
            if(auto esc_nl = lex_ml_basic_escaped_newline::invoke(inner_loc))
            {
                // ignore newline after escape until next non-ws char
            }
            if(inner_loc.iter() == inner_loc.end())
            {
                throw internal_error(format_underline(
                    "parse_ml_basic_string: unexpected end of region",
                    {{source_location(inner_loc), "not sufficient token"}}),
                    source_location(inner_loc));
            }
            delim = lex_ml_basic_string_close::invoke(inner_loc);
        }
        // `lex_ml_basic_string_close` allows 3 to 5 `"`s to allow 1 or 2 `"`s
        // at just before the delimiter. Here, we need to attach `"`s at the
        // end of the string body, if it exists.
        // For detail, see the definition of `lex_ml_basic_string_close`.
        assert(std::all_of(delim.unwrap().first(), delim.unwrap().last(),
                           [](const char c) noexcept {return c == '\"';}));
        switch(delim.unwrap().size())
        {
            case 3: {break;}
            case 4: {retval += "\"";  break;}
            case 5: {retval += "\"\""; break;}
            default:
            {
                throw internal_error(format_underline(
                    "parse_ml_basic_string: closing delimiter has invalid length",
                    {{source_location(inner_loc), "end of this"}}),
                    source_location(inner_loc));
            }
        }

        const auto err_loc = check_utf8_validity(token.unwrap().str());
        if(err_loc == -1)
        {
            return ok(std::make_pair(toml::string(retval), token.unwrap()));
        }
        else
        {
            inner_loc.reset(first);
            inner_loc.advance(err_loc);
            throw syntax_error(format_underline(
                "parse_ml_basic_string: invalid utf8 sequence found",
                {{source_location(inner_loc), "here"}}),
                source_location(inner_loc));
        }
    }
    else
    {
        loc.reset(first);
        return err(format_underline("toml::parse_ml_basic_string: "
                   "the next token is not a valid multiline string",
                   {{source_location(loc), "here"}}));
    }
}

inline result<std::pair<toml::string, region>, std::string>
parse_basic_string(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_basic_string::invoke(loc))
    {
        auto inner_loc = loc;
        inner_loc.reset(first);

        auto quot = lex_quotation_mark::invoke(inner_loc);
        if(!quot)
        {
            throw internal_error(format_underline("parse_basic_string: "
                "invalid token", {{source_location(inner_loc), "should be \""}}),
                source_location(inner_loc));
        }

        std::string retval;
        retval.reserve(token.unwrap().size());

        quot = none();
        while(!quot)
        {
            using lex_unescaped_seq = repeat<lex_basic_unescaped, unlimited>;
            if(auto unescaped = lex_unescaped_seq::invoke(inner_loc))
            {
                retval += unescaped.unwrap().str();
            }
            if(auto escaped = parse_escape_sequence(inner_loc))
            {
                retval += escaped.unwrap();
            }
            if(inner_loc.iter() == inner_loc.end())
            {
                throw internal_error(format_underline(
                    "parse_basic_string: unexpected end of region",
                    {{source_location(inner_loc), "not sufficient token"}}),
                    source_location(inner_loc));
            }
            quot = lex_quotation_mark::invoke(inner_loc);
        }

        const auto err_loc = check_utf8_validity(token.unwrap().str());
        if(err_loc == -1)
        {
            return ok(std::make_pair(toml::string(retval), token.unwrap()));
        }
        else
        {
            inner_loc.reset(first);
            inner_loc.advance(err_loc);
            throw syntax_error(format_underline(
                "parse_basic_string: invalid utf8 sequence found",
                {{source_location(inner_loc), "here"}}),
                source_location(inner_loc));
        }
    }
    else
    {
        loc.reset(first); // rollback
        return err(format_underline("toml::parse_basic_string: "
                   "the next token is not a valid string",
                   {{source_location(loc), "here"}}));
    }
}

inline result<std::pair<toml::string, region>, std::string>
parse_ml_literal_string(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_ml_literal_string::invoke(loc))
    {
        auto inner_loc = loc;
        inner_loc.reset(first);

        const auto open = lex_ml_literal_string_open::invoke(inner_loc);
        if(!open)
        {
            throw internal_error(format_underline(
                "parse_ml_literal_string: invalid token",
                {{source_location(inner_loc), "should be '''"}}),
                source_location(inner_loc));
        }
        // immediate newline is ignored (if exists)
        /* discard return value */ lex_newline::invoke(inner_loc);

        const auto body = lex_ml_literal_body::invoke(inner_loc);

        const auto close = lex_ml_literal_string_close::invoke(inner_loc);
        if(!close)
        {
            throw internal_error(format_underline(
                "parse_ml_literal_string: invalid token",
                {{source_location(inner_loc), "should be '''"}}),
                source_location(inner_loc));
        }
        // `lex_ml_literal_string_close` allows 3 to 5 `'`s to allow 1 or 2 `'`s
        // at just before the delimiter. Here, we need to attach `'`s at the
        // end of the string body, if it exists.
        // For detail, see the definition of `lex_ml_basic_string_close`.

        std::string retval = body.unwrap().str();
        assert(std::all_of(close.unwrap().first(), close.unwrap().last(),
                           [](const char c) noexcept {return c == '\'';}));
        switch(close.unwrap().size())
        {
            case 3: {break;}
            case 4: {retval += "'";  break;}
            case 5: {retval += "''"; break;}
            default:
            {
                throw internal_error(format_underline(
                    "parse_ml_literal_string: closing delimiter has invalid length",
                    {{source_location(inner_loc), "end of this"}}),
                    source_location(inner_loc));
            }
        }

        const auto err_loc = check_utf8_validity(token.unwrap().str());
        if(err_loc == -1)
        {
            return ok(std::make_pair(toml::string(retval, toml::string_t::literal),
                                     token.unwrap()));
        }
        else
        {
            inner_loc.reset(first);
            inner_loc.advance(err_loc);
            throw syntax_error(format_underline(
                "parse_ml_literal_string: invalid utf8 sequence found",
                {{source_location(inner_loc), "here"}}),
                source_location(inner_loc));
        }
    }
    else
    {
        loc.reset(first); // rollback
        return err(format_underline("toml::parse_ml_literal_string: "
                   "the next token is not a valid multiline literal string",
                   {{source_location(loc), "here"}}));
    }
}

inline result<std::pair<toml::string, region>, std::string>
parse_literal_string(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_literal_string::invoke(loc))
    {
        auto inner_loc = loc;
        inner_loc.reset(first);

        const auto open = lex_apostrophe::invoke(inner_loc);
        if(!open)
        {
            throw internal_error(format_underline(
                "parse_literal_string: invalid token",
                {{source_location(inner_loc), "should be '"}}),
                source_location(inner_loc));
        }

        const auto body = repeat<lex_literal_char, unlimited>::invoke(inner_loc);

        const auto close = lex_apostrophe::invoke(inner_loc);
        if(!close)
        {
            throw internal_error(format_underline(
                "parse_literal_string: invalid token",
                {{source_location(inner_loc), "should be '"}}),
                source_location(inner_loc));
        }

        const auto err_loc = check_utf8_validity(token.unwrap().str());
        if(err_loc == -1)
        {
            return ok(std::make_pair(
                      toml::string(body.unwrap().str(), toml::string_t::literal),
                      token.unwrap()));
        }
        else
        {
            inner_loc.reset(first);
            inner_loc.advance(err_loc);
            throw syntax_error(format_underline(
                "parse_literal_string: invalid utf8 sequence found",
                {{source_location(inner_loc), "here"}}),
                source_location(inner_loc));
        }
    }
    else
    {
        loc.reset(first); // rollback
        return err(format_underline("toml::parse_literal_string: "
                   "the next token is not a valid literal string",
                   {{source_location(loc), "here"}}));
    }
}

inline result<std::pair<toml::string, region>, std::string>
parse_string(location& loc)
{
    if(loc.iter() != loc.end() && *(loc.iter()) == '"')
    {
        if(loc.iter() + 1 != loc.end() && *(loc.iter() + 1) == '"' &&
           loc.iter() + 2 != loc.end() && *(loc.iter() + 2) == '"')
        {
            return parse_ml_basic_string(loc);
        }
        else
        {
            return parse_basic_string(loc);
        }
    }
    else if(loc.iter() != loc.end() && *(loc.iter()) == '\'')
    {
        if(loc.iter() + 1 != loc.end() && *(loc.iter() + 1) == '\'' &&
           loc.iter() + 2 != loc.end() && *(loc.iter() + 2) == '\'')
        {
            return parse_ml_literal_string(loc);
        }
        else
        {
            return parse_literal_string(loc);
        }
    }
    return err(format_underline("toml::parse_string: ",
                {{source_location(loc), "the next token is not a string"}}));
}

inline result<std::pair<local_date, region>, std::string>
parse_local_date(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_local_date::invoke(loc))
    {
        location inner_loc(loc.name(), token.unwrap().str());

        const auto y = lex_date_fullyear::invoke(inner_loc);
        if(!y || inner_loc.iter() == inner_loc.end() || *inner_loc.iter() != '-')
        {
            throw internal_error(format_underline(
                "toml::parse_local_date: invalid year format",
                {{source_location(inner_loc), "should be `-`"}}),
                source_location(inner_loc));
        }
        inner_loc.advance();
        const auto m = lex_date_month::invoke(inner_loc);
        if(!m || inner_loc.iter() == inner_loc.end() || *inner_loc.iter() != '-')
        {
            throw internal_error(format_underline(
                "toml::parse_local_date: invalid month format",
                {{source_location(inner_loc), "should be `-`"}}),
                source_location(inner_loc));
        }
        inner_loc.advance();
        const auto d = lex_date_mday::invoke(inner_loc);
        if(!d)
        {
            throw internal_error(format_underline(
                "toml::parse_local_date: invalid day format",
                {{source_location(inner_loc), "here"}}),
                source_location(inner_loc));
        }

        const auto year  = static_cast<std::int16_t>(from_string<int>(y.unwrap().str(), 0));
        const auto month = static_cast<std::int8_t >(from_string<int>(m.unwrap().str(), 0));
        const auto day   = static_cast<std::int8_t >(from_string<int>(d.unwrap().str(), 0));

        // We briefly check whether the input date is valid or not. But here, we
        // only check if the RFC3339 compliance.
        //     Actually there are several special date that does not exist,
        // because of historical reasons, such as 1582/10/5-1582/10/14 (only in
        // several countries). But here, we do not care about such a complicated
        // rule. It makes the code complicated and there is only low probability
        // that such a specific date is needed in practice. If someone need to
        // validate date accurately, that means that the one need a specialized
        // library for their purpose in a different layer.
        {
            const bool is_leap = (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
            const auto max_day = (month == 2) ? (is_leap ? 29 : 28) :
                ((month == 4 || month == 6 || month == 9 || month == 11) ? 30 : 31);

            if((month < 1 || 12 < month) || (day < 1 || max_day < day))
            {
                throw syntax_error(format_underline("toml::parse_date: "
                    "invalid date: it does not conform RFC3339.", {{
                    source_location(loc), "month should be 01-12, day should be"
                    " 01-28,29,30,31, depending on month/year."
                    }}), source_location(inner_loc));
            }
        }
        return ok(std::make_pair(local_date(year, static_cast<month_t>(month - 1), day),
                                 token.unwrap()));
    }
    else
    {
        loc.reset(first);
        return err(format_underline("toml::parse_local_date: ",
            {{source_location(loc), "the next token is not a local_date"}}));
    }
}

inline result<std::pair<local_time, region>, std::string>
parse_local_time(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_local_time::invoke(loc))
    {
        location inner_loc(loc.name(), token.unwrap().str());

        const auto h = lex_time_hour::invoke(inner_loc);
        if(!h || inner_loc.iter() == inner_loc.end() || *inner_loc.iter() != ':')
        {
            throw internal_error(format_underline(
                "toml::parse_local_time: invalid year format",
                {{source_location(inner_loc), "should be `:`"}}),
                source_location(inner_loc));
        }
        inner_loc.advance();
        const auto m = lex_time_minute::invoke(inner_loc);
        if(!m || inner_loc.iter() == inner_loc.end() || *inner_loc.iter() != ':')
        {
            throw internal_error(format_underline(
                "toml::parse_local_time: invalid month format",
                {{source_location(inner_loc), "should be `:`"}}),
                source_location(inner_loc));
        }
        inner_loc.advance();
        const auto s = lex_time_second::invoke(inner_loc);
        if(!s)
        {
            throw internal_error(format_underline(
                "toml::parse_local_time: invalid second format",
                {{source_location(inner_loc), "here"}}),
                source_location(inner_loc));
        }

        const int hour   = from_string<int>(h.unwrap().str(), 0);
        const int minute = from_string<int>(m.unwrap().str(), 0);
        const int second = from_string<int>(s.unwrap().str(), 0);

        if((hour   < 0 || 23 < hour) || (minute < 0 || 59 < minute) ||
           (second < 0 || 60 < second)) // it may be leap second
        {
            throw syntax_error(format_underline("toml::parse_local_time: "
                "invalid time: it does not conform RFC3339.", {{
                source_location(loc), "hour should be 00-23, minute should be"
                " 00-59, second should be 00-60 (depending on the leap"
                " second rules.)"}}), source_location(inner_loc));
        }

        local_time time(hour, minute, second, 0, 0);

        const auto before_secfrac = inner_loc.iter();
        if(const auto secfrac = lex_time_secfrac::invoke(inner_loc))
        {
            auto sf = secfrac.unwrap().str();
            sf.erase(sf.begin()); // sf.front() == '.'
            switch(sf.size() % 3)
            {
                case 2:  sf += '0';  break;
                case 1:  sf += "00"; break;
                case 0:  break;
                default: break;
            }
            if(sf.size() >= 9)
            {
                time.millisecond = from_string<std::uint16_t>(sf.substr(0, 3), 0u);
                time.microsecond = from_string<std::uint16_t>(sf.substr(3, 3), 0u);
                time.nanosecond  = from_string<std::uint16_t>(sf.substr(6, 3), 0u);
            }
            else if(sf.size() >= 6)
            {
                time.millisecond = from_string<std::uint16_t>(sf.substr(0, 3), 0u);
                time.microsecond = from_string<std::uint16_t>(sf.substr(3, 3), 0u);
            }
            else if(sf.size() >= 3)
            {
                time.millisecond = from_string<std::uint16_t>(sf, 0u);
                time.microsecond = 0u;
            }
        }
        else
        {
            if(before_secfrac != inner_loc.iter())
            {
                throw internal_error(format_underline(
                    "toml::parse_local_time: invalid subsecond format",
                    {{source_location(inner_loc), "here"}}),
                source_location(inner_loc));
            }
        }
        return ok(std::make_pair(time, token.unwrap()));
    }
    else
    {
        loc.reset(first);
        return err(format_underline("toml::parse_local_time: ",
            {{source_location(loc), "the next token is not a local_time"}}));
    }
}

inline result<std::pair<local_datetime, region>, std::string>
parse_local_datetime(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_local_date_time::invoke(loc))
    {
        location inner_loc(loc.name(), token.unwrap().str());
        const auto date = parse_local_date(inner_loc);
        if(!date || inner_loc.iter() == inner_loc.end())
        {
            throw internal_error(format_underline(
                "toml::parse_local_datetime: invalid datetime format",
                {{source_location(inner_loc), "date, not datetime"}}),
                source_location(inner_loc));
        }
        const char delim = *(inner_loc.iter());
        if(delim != 'T' && delim != 't' && delim != ' ')
        {
            throw internal_error(format_underline(
                "toml::parse_local_datetime: invalid datetime format",
                {{source_location(inner_loc), "should be `T` or ` ` (space)"}}),
                source_location(inner_loc));
        }
        inner_loc.advance();
        const auto time = parse_local_time(inner_loc);
        if(!time)
        {
            throw internal_error(format_underline(
                "toml::parse_local_datetime: invalid datetime format",
                {{source_location(inner_loc), "invalid time format"}}),
                source_location(inner_loc));
        }
        return ok(std::make_pair(
            local_datetime(date.unwrap().first, time.unwrap().first),
            token.unwrap()));
    }
    else
    {
        loc.reset(first);
        return err(format_underline("toml::parse_local_datetime: ",
            {{source_location(loc), "the next token is not a local_datetime"}}));
    }
}

inline result<std::pair<offset_datetime, region>, std::string>
parse_offset_datetime(location& loc)
{
    const auto first = loc.iter();
    if(const auto token = lex_offset_date_time::invoke(loc))
    {
        location inner_loc(loc.name(), token.unwrap().str());
        const auto datetime = parse_local_datetime(inner_loc);
        if(!datetime || inner_loc.iter() == inner_loc.end())
        {
            throw internal_error(format_underline(
                "toml::parse_offset_datetime: invalid datetime format",
                {{source_location(inner_loc), "date, not datetime"}}),
                source_location(inner_loc));
        }
        time_offset offset(0, 0);
        if(const auto ofs = lex_time_numoffset::invoke(inner_loc))
        {
            const auto str = ofs.unwrap().str();

            const auto hour   = from_string<int>(str.substr(1,2), 0);
            const auto minute = from_string<int>(str.substr(4,2), 0);

            if((hour < 0 || 23 < hour) || (minute < 0 || 59 < minute))
            {
                throw syntax_error(format_underline("toml::parse_offset_datetime: "
                    "invalid offset: it does not conform RFC3339.", {{
                    source_location(loc), "month should be 01-12, day should be"
                    " 01-28,29,30,31, depending on month/year."
                    }}), source_location(inner_loc));
            }

            if(str.front() == '+')
            {
                offset = time_offset(hour, minute);
            }
            else
            {
                offset = time_offset(-hour, -minute);
            }
        }
        else if(*inner_loc.iter() != 'Z' && *inner_loc.iter() != 'z')
        {
            throw internal_error(format_underline(
                "toml::parse_offset_datetime: invalid datetime format",
                {{source_location(inner_loc), "should be `Z` or `+HH:MM`"}}),
                source_location(inner_loc));
        }
        return ok(std::make_pair(offset_datetime(datetime.unwrap().first, offset),
                                 token.unwrap()));
    }
    else
    {
        loc.reset(first);
        return err(format_underline("toml::parse_offset_datetime: ",
            {{source_location(loc), "the next token is not a offset_datetime"}}));
    }
}

inline result<std::pair<key, region>, std::string>
parse_simple_key(location& loc)
{
    if(const auto bstr = parse_basic_string(loc))
    {
        return ok(std::make_pair(bstr.unwrap().first.str, bstr.unwrap().second));
    }
    if(const auto lstr = parse_literal_string(loc))
    {
        return ok(std::make_pair(lstr.unwrap().first.str, lstr.unwrap().second));
    }
    if(const auto bare = lex_unquoted_key::invoke(loc))
    {
        const auto reg = bare.unwrap();
        return ok(std::make_pair(reg.str(), reg));
    }
    return err(format_underline("toml::parse_simple_key: ",
            {{source_location(loc), "the next token is not a simple key"}}));
}

// dotted key become vector of keys
inline result<std::pair<std::vector<key>, region>, std::string>
parse_key(location& loc)
{
    const auto first = loc.iter();
    // dotted key -> `foo.bar.baz` where several single keys are chained by
    // dots. Whitespaces between keys and dots are allowed.
    if(const auto token = lex_dotted_key::invoke(loc))
    {
        const auto reg = token.unwrap();
        location inner_loc(loc.name(), reg.str());
        std::vector<key> keys;

        while(inner_loc.iter() != inner_loc.end())
        {
            lex_ws::invoke(inner_loc);
            if(const auto k = parse_simple_key(inner_loc))
            {
                keys.push_back(k.unwrap().first);
            }
            else
            {
                throw internal_error(format_underline(
                    "toml::parse_key: dotted key contains invalid key",
                    {{source_location(inner_loc), k.unwrap_err()}}),
                    source_location(inner_loc));
            }

            lex_ws::invoke(inner_loc);
            if(inner_loc.iter() == inner_loc.end())
            {
                break;
            }
            else if(*inner_loc.iter() == '.')
            {
                inner_loc.advance(); // to skip `.`
            }
            else
            {
                throw internal_error(format_underline("toml::parse_key: "
                    "dotted key contains invalid key ",
                    {{source_location(inner_loc), "should be `.`"}}),
                    source_location(inner_loc));
            }
        }
        return ok(std::make_pair(keys, reg));
    }
    loc.reset(first);

    // simple_key: a single (basic_string|literal_string|bare key)
    if(const auto smpl = parse_simple_key(loc))
    {
        return ok(std::make_pair(std::vector<key>(1, smpl.unwrap().first),
                                 smpl.unwrap().second));
    }
    return err(format_underline("toml::parse_key: an invalid key appeared.",
                {{source_location(loc), "is not a valid key"}}, {
                "bare keys  : non-empty strings composed only of [A-Za-z0-9_-].",
                "quoted keys: same as \"basic strings\" or 'literal strings'.",
                "dotted keys: sequence of bare or quoted keys joined with a dot."
                }));
}

// forward-decl to implement parse_array and parse_table
template<typename Value>
result<Value, std::string> parse_value(location&, const std::size_t n_rec);

template<typename Value>
result<std::pair<typename Value::array_type, region>, std::string>
parse_array(location& loc, const std::size_t n_rec)
{
    using value_type = Value;
    using array_type = typename value_type::array_type;

    if(n_rec > TOML11_VALUE_RECURSION_LIMIT)
    {
        // parse_array does not have any way to handle recursive error currently...
        throw syntax_error(std::string("toml::parse_array: recursion limit ("
                TOML11_STRINGIZE(TOML11_VALUE_RECURSION_LIMIT) ") exceeded"),
                source_location(loc));
    }

    const auto first = loc.iter();
    if(loc.iter() == loc.end())
    {
        return err("toml::parse_array: input is empty");
    }
    if(*loc.iter() != '[')
    {
        return err("toml::parse_array: token is not an array");
    }
    loc.advance();

    using lex_ws_comment_newline = repeat<
        either<lex_wschar, lex_newline, lex_comment>, unlimited>;

    array_type retval;
    while(loc.iter() != loc.end())
    {
        lex_ws_comment_newline::invoke(loc); // skip

        if(loc.iter() != loc.end() && *loc.iter() == ']')
        {
            loc.advance(); // skip ']'
            return ok(std::make_pair(retval,
                      region(loc, first, loc.iter())));
        }

        if(auto val = parse_value<value_type>(loc, n_rec+1))
        {
            // After TOML v1.0.0-rc.1, array becomes to be able to have values
            // with different types. So here we will omit this by default.
            //
            // But some of the test-suite checks if the parser accepts a hetero-
            // geneous arrays, so we keep this for a while.
#ifdef TOML11_DISALLOW_HETEROGENEOUS_ARRAYS
            if(!retval.empty() && retval.front().type() != val.as_ok().type())
            {
                auto array_start_loc = loc;
                array_start_loc.reset(first);

                throw syntax_error(format_underline("toml::parse_array: "
                    "type of elements should be the same each other.", {
                        {source_location(array_start_loc), "array starts here"},
                        {
                            retval.front().location(),
                            "value has type " + stringize(retval.front().type())
                        },
                        {
                            val.unwrap().location(),
                            "value has different type, " + stringize(val.unwrap().type())
                        }
                    }), source_location(loc));
            }
#endif
            retval.push_back(std::move(val.unwrap()));
        }
        else
        {
            auto array_start_loc = loc;
            array_start_loc.reset(first);

            throw syntax_error(format_underline("toml::parse_array: "
                "value having invalid format appeared in an array", {
                    {source_location(array_start_loc), "array starts here"},
                    {source_location(loc), "it is not a valid value."}
                }), source_location(loc));
        }

        using lex_array_separator = sequence<maybe<lex_ws_comment_newline>, character<','>>;
        const auto sp = lex_array_separator::invoke(loc);
        if(!sp)
        {
            lex_ws_comment_newline::invoke(loc);
            if(loc.iter() != loc.end() && *loc.iter() == ']')
            {
                loc.advance(); // skip ']'
                return ok(std::make_pair(retval,
                          region(loc, first, loc.iter())));
            }
            else
            {
                auto array_start_loc = loc;
                array_start_loc.reset(first);

                throw syntax_error(format_underline("toml::parse_array:"
                    " missing array separator `,` after a value", {
                        {source_location(array_start_loc), "array starts here"},
                        {source_location(loc),             "should be `,`"}
                    }), source_location(loc));
            }
        }
    }
    loc.reset(first);
    throw syntax_error(format_underline("toml::parse_array: "
            "array did not closed by `]`",
            {{source_location(loc), "should be closed"}}),
            source_location(loc));
}

template<typename Value>
result<std::pair<std::pair<std::vector<key>, region>, Value>, std::string>
parse_key_value_pair(location& loc, const std::size_t n_rec)
{
    using value_type = Value;

    const auto first = loc.iter();
    auto key_reg = parse_key(loc);
    if(!key_reg)
    {
        std::string msg = std::move(key_reg.unwrap_err());
        // if the next token is keyvalue-separator, it means that there are no
        // key. then we need to show error as "empty key is not allowed".
        if(const auto keyval_sep = lex_keyval_sep::invoke(loc))
        {
            loc.reset(first);
            msg = format_underline("toml::parse_key_value_pair: "
                "empty key is not allowed.",
                {{source_location(loc), "key expected before '='"}});
        }
        return err(std::move(msg));
    }

    const auto kvsp = lex_keyval_sep::invoke(loc);
    if(!kvsp)
    {
        std::string msg;
        // if the line contains '=' after the invalid sequence, possibly the
        // error is in the key (like, invalid character in bare key).
        const auto line_end = std::find(loc.iter(), loc.end(), '\n');
        if(std::find(loc.iter(), line_end, '=') != line_end)
        {
            msg = format_underline("toml::parse_key_value_pair: "
                "invalid format for key",
                {{source_location(loc), "invalid character in key"}},
                {"Did you forget '.' to separate dotted-key?",
                "Allowed characters for bare key are [0-9a-zA-Z_-]."});
        }
        else // if not, the error is lack of key-value separator.
        {
            msg = format_underline("toml::parse_key_value_pair: "
                "missing key-value separator `=`",
                {{source_location(loc), "should be `=`"}});
        }
        loc.reset(first);
        return err(std::move(msg));
    }

    const auto after_kvsp = loc.iter(); // err msg
    auto val = parse_value<value_type>(loc, n_rec);
    if(!val)
    {
        std::string msg;
        loc.reset(after_kvsp);
        // check there is something not a comment/whitespace after `=`
        if(sequence<maybe<lex_ws>, maybe<lex_comment>, lex_newline>::invoke(loc))
        {
            loc.reset(after_kvsp);
            msg = format_underline("toml::parse_key_value_pair: "
                    "missing value after key-value separator '='",
                    {{source_location(loc), "expected value, but got nothing"}});
        }
        else // there is something not a comment/whitespace, so invalid format.
        {
            msg = std::move(val.unwrap_err());
        }
        loc.reset(first);
        return err(msg);
    }
    return ok(std::make_pair(std::move(key_reg.unwrap()),
                             std::move(val.unwrap())));
}

// for error messages.
template<typename InputIterator>
std::string format_dotted_keys(InputIterator first, const InputIterator last)
{
    static_assert(std::is_same<key,
        typename std::iterator_traits<InputIterator>::value_type>::value,"");

    std::string retval(*first++);
    for(; first != last; ++first)
    {
        retval += '.';
        retval += *first;
    }
    return retval;
}

// forward decl for is_valid_forward_table_definition
result<std::pair<std::vector<key>, region>, std::string>
parse_table_key(location& loc);
result<std::pair<std::vector<key>, region>, std::string>
parse_array_table_key(location& loc);
template<typename Value>
result<std::pair<typename Value::table_type, region>, std::string>
parse_inline_table(location& loc, const std::size_t n_rec);

// The following toml file is allowed.
// ```toml
// [a.b.c]     # here, table `a` has element `b`.
// foo = "bar"
// [a]         # merge a = {baz = "qux"} to a = {b = {...}}
// baz = "qux"
// ```
// But the following is not allowed.
// ```toml
// [a]
// b.c.foo = "bar"
// [a]             # error! the same table [a] defined!
// baz = "qux"
// ```
// The following is neither allowed.
// ```toml
// a = { b.c.foo = "bar"}
// [a]             # error! the same table [a] defined!
// baz = "qux"
// ```
// Here, it parses region of `tab->at(k)` as a table key and check the depth
// of the key. If the key region points deeper node, it would be allowed.
// Otherwise, the key points the same node. It would be rejected.
template<typename Value, typename Iterator>
bool is_valid_forward_table_definition(const Value& fwd, const Value& inserting,
        Iterator key_first, Iterator key_curr, Iterator key_last)
{
    // ------------------------------------------------------------------------
    // check type of the value to be inserted/merged

    std::string inserting_reg = "";
    if(const auto ptr = detail::get_region(inserting))
    {
        inserting_reg = ptr->str();
    }
    location inserting_def("internal", std::move(inserting_reg));
    if(const auto inlinetable = parse_inline_table<Value>(inserting_def, 0))
    {
        // check if we are overwriting existing table.
        // ```toml
        // # NG
        // a.b = 42
        // a = {d = 3.14}
        // ```
        // Inserting an inline table to a existing super-table is not allowed in
        // any case. If we found it, we can reject it without further checking.
        return false;
    }

    // Valid and invalid cases when inserting to the [a.b] table:
    //
    // ## Invalid
    //
    // ```toml
    // # invalid
    // [a]
    // b.c.d = "foo"
    // [a.b]       # a.b is already defined and closed
    // d = "bar"
    // ```
    // ```toml
    // # invalid
    // a = {b.c.d = "foo"}
    // [a.b] # a is already defined and inline table is closed
    // d = "bar"
    // ```
    // ```toml
    // # invalid
    // a.b.c.d = "foo"
    // [a.b] # a.b is already defined and dotted-key table is closed
    // d = "bar"
    // ```
    //
    // ## Valid
    //
    // ```toml
    // # OK. a.b is defined, but is *overwritable*
    // [a.b.c]
    // d = "foo"
    // [a.b]
    // d = "bar"
    // ```
    // ```toml
    // # OK. a.b is defined, but is *overwritable*
    // [a]
    // b.c.d = "foo"
    // b.e = "bar"
    // ```

    // ------------------------------------------------------------------------
    // check table defined before

    std::string internal = "";
    if(const auto ptr = detail::get_region(fwd))
    {
        internal = ptr->str();
    }
    location def("internal", std::move(internal));
    if(const auto tabkeys = parse_table_key(def)) // [table.key]
    {
        // table keys always contains all the nodes from the root.
        const auto& tks = tabkeys.unwrap().first;
        if(std::size_t(std::distance(key_first, key_last)) == tks.size() &&
           std::equal(tks.begin(), tks.end(), key_first))
        {
            // the keys are equivalent. it is not allowed.
            return false;
        }
        // the keys are not equivalent. it is allowed.
        return true;
    }
    // nested array-of-table definition implicitly defines tables.
    // those tables can be reopened.
    if(const auto atabkeys = parse_array_table_key(def))
    {
        // table keys always contains all the nodes from the root.
        const auto& tks = atabkeys.unwrap().first;
        if(std::size_t(std::distance(key_first, key_last)) == tks.size() &&
           std::equal(tks.begin(), tks.end(), key_first))
        {
            // the keys are equivalent. it is not allowed.
            return false;
        }
        // the keys are not equivalent. it is allowed.
        return true;
    }
    if(const auto dotkeys = parse_key(def)) // a.b.c = "foo"
    {
        // consider the following case.
        // [a]
        // b.c = {d = 42}
        // [a.b.c]
        // e = 2.71
        // this defines the table [a.b.c] twice. no?
        if(const auto reopening_dotkey_by_table = parse_table_key(inserting_def))
        {
            // re-opening a dotkey-defined table by a table is invalid.
            // only dotkey can append a key-val. Like:
            // ```toml
            // a.b.c = "foo"
            // a.b.d = "bar" # OK. reopen `a.b` by dotkey
            // [a.b]
            // e = "bar" # Invalid. re-opening `a.b` by [a.b] is not allowed.
            // ```
            return false;
        }

        // a dotted key starts from the node representing a table in which the
        // dotted key belongs to.
        const auto& dks = dotkeys.unwrap().first;
        if(std::size_t(std::distance(key_curr, key_last)) == dks.size() &&
           std::equal(dks.begin(), dks.end(), key_curr))
        {
            // the keys are equivalent. it is not allowed.
            return false;
        }
        // the keys are not equivalent. it is allowed.
        return true;
    }
    return false;
}

template<typename Value, typename InputIterator>
result<bool, std::string>
insert_nested_key(typename Value::table_type& root, const Value& v,
                  InputIterator iter, const InputIterator last,
                  region key_reg,
                  const bool is_array_of_table = false)
{
    static_assert(std::is_same<key,
        typename std::iterator_traits<InputIterator>::value_type>::value,"");

    using value_type = Value;
    using table_type = typename value_type::table_type;
    using array_type = typename value_type::array_type;

    const auto first = iter;
    assert(iter != last);

    table_type* tab = std::addressof(root);
    for(; iter != last; ++iter) // search recursively
    {
        const key& k = *iter;
        if(std::next(iter) == last) // k is the last key
        {
            // XXX if the value is array-of-tables, there can be several
            //     tables that are in the same array. in that case, we need to
            //     find the last element and insert it to there.
            if(is_array_of_table)
            {
                if(tab->count(k) == 1) // there is already an array of table
                {
                    if(tab->at(k).is_table())
                    {
                        // show special err msg for conflicting table
                        throw syntax_error(format_underline(concat_to_string(
                            "toml::insert_value: array of table (\"",
                            format_dotted_keys(first, last),
                            "\") cannot be defined"), {
                                {tab->at(k).location(), "table already defined"},
                                {v.location(), "this conflicts with the previous table"}
                            }), v.location());
                    }
                    else if(!(tab->at(k).is_array()))
                    {
                        throw syntax_error(format_underline(concat_to_string(
                            "toml::insert_value: array of table (\"",
                            format_dotted_keys(first, last), "\") collides with"
                            " existing value"), {
                                {tab->at(k).location(),
                                 concat_to_string("this ", tab->at(k).type(),
                                                  " value already exists")},
                                {v.location(),
                                 "while inserting this array-of-tables"}
                            }), v.location());
                    }
                    // the above if-else-if checks tab->at(k) is an array
                    auto& a = tab->at(k).as_array();
                    // If table element is defined as [[array_of_tables]], it
                    // cannot be an empty array. If an array of tables is
                    // defined as `aot = []`, it cannot be appended.
                    if(a.empty() || !(a.front().is_table()))
                    {
                        throw syntax_error(format_underline(concat_to_string(
                            "toml::insert_value: array of table (\"",
                            format_dotted_keys(first, last), "\") collides with"
                            " existing value"), {
                                {tab->at(k).location(),
                                 concat_to_string("this ", tab->at(k).type(),
                                                  " value already exists")},
                                {v.location(),
                                 "while inserting this array-of-tables"}
                            }), v.location());
                    }
                    // avoid conflicting array of table like the following.
                    // ```toml
                    // a = [{b = 42}] # define a as an array of *inline* tables
                    // [[a]]          # a is an array of *multi-line* tables
                    // b = 54
                    // ```
                    // Here, from the type information, these cannot be detected
                    // because inline table is also a table.
                    // But toml v0.5.0 explicitly says it is invalid. The above
                    // array-of-tables has a static size and appending to the
                    // array is invalid.
                    // In this library, multi-line table value has a region
                    // that points to the key of the table (e.g. [[a]]). By
                    // comparing the first two letters in key, we can detect
                    // the array-of-table is inline or multiline.
                    if(const auto ptr = detail::get_region(a.front()))
                    {
                        if(ptr->str().substr(0,2) != "[[")
                        {
                            throw syntax_error(format_underline(concat_to_string(
                                "toml::insert_value: array of table (\"",
                                format_dotted_keys(first, last), "\") collides "
                                "with existing array-of-tables"), {
                                    {tab->at(k).location(),
                                     concat_to_string("this ", tab->at(k).type(),
                                                      " value has static size")},
                                    {v.location(),
                                     "appending it to the statically sized array"}
                                }), v.location());
                        }
                    }
                    a.push_back(v);
                    return ok(true);
                }
                else // if not, we need to create the array of table
                {
                    // XXX: Consider the following array of tables.
                    // ```toml
                    // # This is a comment.
                    // [[aot]]
                    // foo = "bar"
                    // ```
                    // Here, the comment is for `aot`. But here, actually two
                    // values are defined. An array that contains tables, named
                    // `aot`, and the 0th element of the `aot`, `{foo = "bar"}`.
                    // Those two are different from each other. But both of them
                    // points to the same portion of the TOML file, `[[aot]]`,
                    // so `key_reg.comments()` returns `# This is a comment`.
                    // If it is assigned as a comment of `aot` defined here, the
                    // comment will be duplicated. Both the `aot` itself and
                    // the 0-th element will have the same comment. This causes
                    // "duplication of the same comments" bug when the data is
                    // serialized.
                    //     Next, consider the following.
                    // ```toml
                    // # comment 1
                    // aot = [
                    //     # comment 2
                    //     {foo = "bar"},
                    // ]
                    // ```
                    // In this case, we can distinguish those two comments. So
                    // here we need to add "comment 1" to the `aot` and
                    // "comment 2" to the 0th element of that.
                    //     To distinguish those two, we check the key region.
                    std::vector<std::string> comments{/* empty by default */};
                    if(key_reg.str().substr(0, 2) != "[[")
                    {
                        comments = key_reg.comments();
                    }
                    value_type aot(array_type(1, v), key_reg, std::move(comments));
                    tab->insert(std::make_pair(k, aot));
                    return ok(true);
                }
            } // end if(array of table)

            if(tab->count(k) == 1)
            {
                if(tab->at(k).is_table() && v.is_table())
                {
                    if(!is_valid_forward_table_definition(
                                tab->at(k), v, first, iter, last))
                    {
                        throw syntax_error(format_underline(concat_to_string(
                            "toml::insert_value: table (\"",
                            format_dotted_keys(first, last),
                            "\") already exists."), {
                                {tab->at(k).location(), "table already exists here"},
                                {v.location(), "table defined twice"}
                            }), v.location());
                    }
                    // to allow the following toml file.
                    // [a.b.c]
                    // d = 42
                    // [a]
                    // e = 2.71
                    auto& t = tab->at(k).as_table();
                    for(const auto& kv : v.as_table())
                    {
                        if(tab->at(k).contains(kv.first))
                        {
                            throw syntax_error(format_underline(concat_to_string(
                                "toml::insert_value: value (\"",
                                format_dotted_keys(first, last),
                                "\") already exists."), {
                                    {t.at(kv.first).location(), "already exists here"},
                                    {v.location(), "this defined twice"}
                                }), v.location());
                        }
                        t[kv.first] = kv.second;
                    }
                    detail::change_region(tab->at(k), key_reg);
                    return ok(true);
                }
                else if(v.is_table()                     &&
                        tab->at(k).is_array()            &&
                        tab->at(k).as_array().size() > 0 &&
                        tab->at(k).as_array().front().is_table())
                {
                    throw syntax_error(format_underline(concat_to_string(
                        "toml::insert_value: array of tables (\"",
                        format_dotted_keys(first, last), "\") already exists."), {
                            {tab->at(k).location(), "array of tables defined here"},
                            {v.location(), "table conflicts with the previous array of table"}
                        }), v.location());
                }
                else
                {
                    throw syntax_error(format_underline(concat_to_string(
                        "toml::insert_value: value (\"",
                        format_dotted_keys(first, last), "\") already exists."), {
                            {tab->at(k).location(), "value already exists here"},
                            {v.location(), "value defined twice"}
                        }), v.location());
                }
            }
            tab->insert(std::make_pair(k, v));
            return ok(true);
        }
        else // k is not the last one, we should insert recursively
        {
            // if there is no corresponding value, insert it first.
            // related: you don't need to write
            // # [x]
            // # [x.y]
            // to write
            // [x.y.z]
            if(tab->count(k) == 0)
            {
                // a table that is defined implicitly doesn't have any comments.
                (*tab)[k] = value_type(table_type{}, key_reg, {/*no comment*/});
            }

            // type checking...
            if(tab->at(k).is_table())
            {
                // According to toml-lang/toml:36d3091b3 "Clarify that inline
                // tables are immutable", check if it adds key-value pair to an
                // inline table.
                if(const auto* ptr = get_region(tab->at(k)))
                {
                    // here, if the value is a (multi-line) table, the region
                    // should be something like `[table-name]`.
                    if(ptr->front() == '{')
                    {
                        throw syntax_error(format_underline(concat_to_string(
                            "toml::insert_value: inserting to an inline table (",
                            format_dotted_keys(first, std::next(iter)),
                            ") but inline tables are immutable"), {
                                {tab->at(k).location(), "inline tables are immutable"},
                                {v.location(), "inserting this"}
                            }), v.location());
                    }
                }
                tab = std::addressof((*tab)[k].as_table());
            }
            else if(tab->at(k).is_array()) // inserting to array-of-tables?
            {
                auto& a = (*tab)[k].as_array();
                if(!a.back().is_table())
                {
                    throw syntax_error(format_underline(concat_to_string(
                        "toml::insert_value: target (",
                        format_dotted_keys(first, std::next(iter)),
                        ") is neither table nor an array of tables"), {
                            {a.back().location(), concat_to_string(
                                    "actual type is ", a.back().type())},
                            {v.location(), "inserting this"}
                        }), v.location());
                }
                if(a.empty())
                {
                    throw syntax_error(format_underline(concat_to_string(
                        "toml::insert_value: table (\"",
                        format_dotted_keys(first, last), "\") conflicts with"
                        " existing value"), {
                            {tab->at(k).location(), std::string("this array is not insertable")},
                            {v.location(), std::string("appending it to the statically sized array")}
                        }), v.location());
                }
                if(const auto ptr = detail::get_region(a.at(0)))
                {
                    if(ptr->str().substr(0,2) != "[[")
                    {
                        throw syntax_error(format_underline(concat_to_string(
                            "toml::insert_value: a table (\"",
                            format_dotted_keys(first, last), "\") cannot be "
                            "inserted to an existing inline array-of-tables"), {
                                {tab->at(k).location(), std::string("this array of table has a static size")},
                                {v.location(), std::string("appending it to the statically sized array")}
                            }), v.location());
                    }
                }
                tab = std::addressof(a.back().as_table());
            }
            else
            {
                throw syntax_error(format_underline(concat_to_string(
                    "toml::insert_value: target (",
                    format_dotted_keys(first, std::next(iter)),
                    ") is neither table nor an array of tables"), {
                        {tab->at(k).location(), concat_to_string(
                                "actual type is ", tab->at(k).type())},
                        {v.location(), "inserting this"}
                    }), v.location());
            }
        }
    }
    return err(std::string("toml::detail::insert_nested_key: never reach here"));
}

template<typename Value>
result<std::pair<typename Value::table_type, region>, std::string>
parse_inline_table(location& loc, const std::size_t n_rec)
{
    using value_type = Value;
    using table_type = typename value_type::table_type;

    if(n_rec > TOML11_VALUE_RECURSION_LIMIT)
    {
        throw syntax_error(std::string("toml::parse_inline_table: recursion limit ("
                TOML11_STRINGIZE(TOML11_VALUE_RECURSION_LIMIT) ") exceeded"),
                source_location(loc));
    }

    const auto first = loc.iter();
    table_type retval;
    if(!(loc.iter() != loc.end() && *loc.iter() == '{'))
    {
        return err(format_underline("toml::parse_inline_table: ",
            {{source_location(loc), "the next token is not an inline table"}}));
    }
    loc.advance();

    // check if the inline table is an empty table = { }
    maybe<lex_ws>::invoke(loc);
    if(loc.iter() != loc.end() && *loc.iter() == '}')
    {
        loc.advance(); // skip `}`
        return ok(std::make_pair(retval, region(loc, first, loc.iter())));
    }

    // it starts from "{". it should be formatted as inline-table
    while(loc.iter() != loc.end())
    {
        const auto kv_r = parse_key_value_pair<value_type>(loc, n_rec+1);
        if(!kv_r)
        {
            return err(kv_r.unwrap_err());
        }

        const auto&              kvpair  = kv_r.unwrap();
        const std::vector<key>&  keys    = kvpair.first.first;
        const auto&              key_reg = kvpair.first.second;
        const value_type&        val     = kvpair.second;

        const auto inserted =
            insert_nested_key(retval, val, keys.begin(), keys.end(), key_reg);
        if(!inserted)
        {
            throw internal_error("toml::parse_inline_table: "
                "failed to insert value into table: " + inserted.unwrap_err(),
                source_location(loc));
        }

        using lex_table_separator = sequence<maybe<lex_ws>, character<','>>;
        const auto sp = lex_table_separator::invoke(loc);

        if(!sp)
        {
            maybe<lex_ws>::invoke(loc);

            if(loc.iter() == loc.end())
            {
                throw syntax_error(format_underline(
                    "toml::parse_inline_table: missing table separator `}` ",
                    {{source_location(loc), "should be `}`"}}),
                    source_location(loc));
            }
            else if(*loc.iter() == '}')
            {
                loc.advance(); // skip `}`
                return ok(std::make_pair(
                            retval, region(loc, first, loc.iter())));
            }
            else if(*loc.iter() == '#' || *loc.iter() == '\r' || *loc.iter() == '\n')
            {
                throw syntax_error(format_underline(
                    "toml::parse_inline_table: missing curly brace `}`",
                    {{source_location(loc), "should be `}`"}}),
                    source_location(loc));
            }
            else
            {
                throw syntax_error(format_underline(
                    "toml::parse_inline_table: missing table separator `,` ",
                    {{source_location(loc), "should be `,`"}}),
                    source_location(loc));
            }
        }
        else // `,` is found
        {
            maybe<lex_ws>::invoke(loc);
            if(loc.iter() != loc.end() && *loc.iter() == '}')
            {
                throw syntax_error(format_underline(
                    "toml::parse_inline_table: trailing comma is not allowed in"
                    " an inline table",
                    {{source_location(loc), "should be `}`"}}),
                    source_location(loc));
            }
        }
    }
    loc.reset(first);
    throw syntax_error(format_underline("toml::parse_inline_table: "
            "inline table did not closed by `}`",
            {{source_location(loc), "should be closed"}}),
            source_location(loc));
}

inline result<value_t, std::string> guess_number_type(const location& l)
{
    // This function tries to find some (common) mistakes by checking characters
    // that follows the last character of a value. But it is often difficult
    // because some non-newline characters can appear after a value. E.g.
    // spaces, tabs, commas (in an array or inline table), closing brackets
    // (of an array or inline table), comment-sign (#). Since this function
    // does not parse further, those characters are always allowed to be there.
    location loc = l;

    if(lex_offset_date_time::invoke(loc)) {return ok(value_t::offset_datetime);}
    loc.reset(l.iter());

    if(lex_local_date_time::invoke(loc))
    {
        // bad offset may appear after this.
        if(loc.iter() != loc.end() && (*loc.iter() == '+' || *loc.iter() == '-'
                    || *loc.iter() == 'Z' || *loc.iter() == 'z'))
        {
            return err(format_underline("bad offset: should be [+-]HH:MM or Z",
                        {{source_location(loc), "[+-]HH:MM or Z"}},
                        {"pass: +09:00, -05:30", "fail: +9:00, -5:30"}));
        }
        return ok(value_t::local_datetime);
    }
    loc.reset(l.iter());

    if(lex_local_date::invoke(loc))
    {
        // bad time may appear after this.
        // A space is allowed as a delimiter between local time. But there are
        // both cases in which a space becomes valid or invalid.
        // - invalid: 2019-06-16 7:00:00
        // - valid  : 2019-06-16 07:00:00
        if(loc.iter() != loc.end())
        {
            const auto c = *loc.iter();
            if(c == 'T' || c == 't')
            {
                return err(format_underline("bad time: should be HH:MM:SS.subsec",
                        {{source_location(loc), "HH:MM:SS.subsec"}},
                        {"pass: 1979-05-27T07:32:00, 1979-05-27 07:32:00.999999",
                         "fail: 1979-05-27T7:32:00, 1979-05-27 17:32"}));
            }
            if('0' <= c && c <= '9')
            {
                return err(format_underline("bad time: missing T",
                        {{source_location(loc), "T or space required here"}},
                        {"pass: 1979-05-27T07:32:00, 1979-05-27 07:32:00.999999",
                         "fail: 1979-05-27T7:32:00, 1979-05-27 7:32"}));
            }
            if(c == ' ' && std::next(loc.iter()) != loc.end() &&
                ('0' <= *std::next(loc.iter()) && *std::next(loc.iter())<= '9'))
            {
                loc.advance();
                return err(format_underline("bad time: should be HH:MM:SS.subsec",
                        {{source_location(loc), "HH:MM:SS.subsec"}},
                        {"pass: 1979-05-27T07:32:00, 1979-05-27 07:32:00.999999",
                         "fail: 1979-05-27T7:32:00, 1979-05-27 7:32"}));
            }
        }
        return ok(value_t::local_date);
    }
    loc.reset(l.iter());

    if(lex_local_time::invoke(loc)) {return ok(value_t::local_time);}
    loc.reset(l.iter());

    if(lex_float::invoke(loc))
    {
        if(loc.iter() != loc.end() && *loc.iter() == '_')
        {
            return err(format_underline("bad float: `_` should be surrounded by digits",
                        {{source_location(loc), "here"}},
                        {"pass: +1.0, -2e-2, 3.141_592_653_589, inf, nan",
                         "fail: .0, 1., _1.0, 1.0_, 1_.0, 1.0__0"}));
        }
        return ok(value_t::floating);
    }
    loc.reset(l.iter());

    if(lex_integer::invoke(loc))
    {
        if(loc.iter() != loc.end())
        {
            const auto c = *loc.iter();
            if(c == '_')
            {
                return err(format_underline("bad integer: `_` should be surrounded by digits",
                            {{source_location(loc), "here"}},
                            {"pass: -42, 1_000, 1_2_3_4_5, 0xC0FFEE, 0b0010, 0o755",
                             "fail: 1__000, 0123"}));
            }
            if('0' <= c && c <= '9')
            {
                // leading zero. point '0'
                loc.retrace();
                return err(format_underline("bad integer: leading zero",
                            {{source_location(loc), "here"}},
                            {"pass: -42, 1_000, 1_2_3_4_5, 0xC0FFEE, 0b0010, 0o755",
                             "fail: 1__000, 0123"}));
            }
            if(c == ':' || c == '-')
            {
                return err(format_underline("bad datetime: invalid format",
                            {{source_location(loc), "here"}},
                            {"pass: 1979-05-27T07:32:00-07:00, 1979-05-27 07:32:00.999999Z",
                             "fail: 1979-05-27T7:32:00-7:00, 1979-05-27 7:32-00:30"}));
            }
            if(c == '.' || c == 'e' || c == 'E')
            {
                return err(format_underline("bad float: invalid format",
                            {{source_location(loc), "here"}},
                            {"pass: +1.0, -2e-2, 3.141_592_653_589, inf, nan",
                             "fail: .0, 1., _1.0, 1.0_, 1_.0, 1.0__0"}));
            }
        }
        return ok(value_t::integer);
    }
    if(loc.iter() != loc.end() && *loc.iter() == '.')
    {
        return err(format_underline("bad float: invalid format",
                {{source_location(loc), "integer part required before this"}},
                {"pass: +1.0, -2e-2, 3.141_592_653_589, inf, nan",
                 "fail: .0, 1., _1.0, 1.0_, 1_.0, 1.0__0"}));
    }
    if(loc.iter() != loc.end() && *loc.iter() == '_')
    {
        return err(format_underline("bad number: `_` should be surrounded by digits",
                {{source_location(loc), "`_` is not surrounded by digits"}},
                {"pass: -42, 1_000, 1_2_3_4_5, 0xC0FFEE, 0b0010, 0o755",
                 "fail: 1__000, 0123"}));
    }
    return err(format_underline("bad format: unknown value appeared",
                {{source_location(loc), "here"}}));
}

inline result<value_t, std::string> guess_value_type(const location& loc)
{
    switch(*loc.iter())
    {
        case '"' : {return ok(value_t::string);  }
        case '\'': {return ok(value_t::string);  }
        case 't' : {return ok(value_t::boolean); }
        case 'f' : {return ok(value_t::boolean); }
        case '[' : {return ok(value_t::array);   }
        case '{' : {return ok(value_t::table);   }
        case 'i' : {return ok(value_t::floating);} // inf.
        case 'n' : {return ok(value_t::floating);} // nan.
        default  : {return guess_number_type(loc);}
    }
}

template<typename Value, typename T>
result<Value, std::string>
parse_value_helper(result<std::pair<T, region>, std::string> rslt)
{
    if(rslt.is_ok())
    {
        auto comments = rslt.as_ok().second.comments();
        return ok(Value(std::move(rslt.as_ok()), std::move(comments)));
    }
    else
    {
        return err(std::move(rslt.as_err()));
    }
}

template<typename Value>
result<Value, std::string> parse_value(location& loc, const std::size_t n_rec)
{
    const auto first = loc.iter();
    if(first == loc.end())
    {
        return err(format_underline("toml::parse_value: input is empty",
                   {{source_location(loc), ""}}));
    }

    const auto type = guess_value_type(loc);
    if(!type)
    {
        return err(type.unwrap_err());
    }

    switch(type.unwrap())
    {
        case value_t::boolean        : {return parse_value_helper<Value>(parse_boolean(loc)            );}
        case value_t::integer        : {return parse_value_helper<Value>(parse_integer(loc)            );}
        case value_t::floating       : {return parse_value_helper<Value>(parse_floating(loc)           );}
        case value_t::string         : {return parse_value_helper<Value>(parse_string(loc)             );}
        case value_t::offset_datetime: {return parse_value_helper<Value>(parse_offset_datetime(loc)    );}
        case value_t::local_datetime : {return parse_value_helper<Value>(parse_local_datetime(loc)     );}
        case value_t::local_date     : {return parse_value_helper<Value>(parse_local_date(loc)         );}
        case value_t::local_time     : {return parse_value_helper<Value>(parse_local_time(loc)         );}
        case value_t::array          : {return parse_value_helper<Value>(parse_array<Value>(loc, n_rec));}
        case value_t::table          : {return parse_value_helper<Value>(parse_inline_table<Value>(loc, n_rec));}
        default:
        {
            const auto msg = format_underline("toml::parse_value: "
                    "unknown token appeared", {{source_location(loc), "unknown"}});
            loc.reset(first);
            return err(msg);
        }
    }
}

inline result<std::pair<std::vector<key>, region>, std::string>
parse_table_key(location& loc)
{
    if(auto token = lex_std_table::invoke(loc))
    {
        location inner_loc(loc.name(), token.unwrap().str());

        const auto open = lex_std_table_open::invoke(inner_loc);
        if(!open || inner_loc.iter() == inner_loc.end())
        {
            throw internal_error(format_underline(
                "toml::parse_table_key: no `[`",
                {{source_location(inner_loc), "should be `[`"}}),
                source_location(inner_loc));
        }
        // to skip [ a . b . c ]
        //          ^----------- this whitespace
        lex_ws::invoke(inner_loc);
        const auto keys = parse_key(inner_loc);
        if(!keys)
        {
            throw internal_error(format_underline(
                "toml::parse_table_key: invalid key",
                {{source_location(inner_loc), "not key"}}),
                source_location(inner_loc));
        }
        // to skip [ a . b . c ]
        //                    ^-- this whitespace
        lex_ws::invoke(inner_loc);
        const auto close = lex_std_table_close::invoke(inner_loc);
        if(!close)
        {
            throw internal_error(format_underline(
                "toml::parse_table_key: no `]`",
                {{source_location(inner_loc), "should be `]`"}}),
                source_location(inner_loc));
        }

        // after [table.key], newline or EOF(empty table) required.
        if(loc.iter() != loc.end())
        {
            using lex_newline_after_table_key =
                sequence<maybe<lex_ws>, maybe<lex_comment>, lex_newline>;
            const auto nl = lex_newline_after_table_key::invoke(loc);
            if(!nl)
            {
                throw syntax_error(format_underline(
                    "toml::parse_table_key: newline required after [table.key]",
                    {{source_location(loc), "expected newline"}}),
                    source_location(loc));
            }
        }
        return ok(std::make_pair(keys.unwrap().first, token.unwrap()));
    }
    else
    {
        return err(format_underline("toml::parse_table_key: "
            "not a valid table key", {{source_location(loc), "here"}}));
    }
}

inline result<std::pair<std::vector<key>, region>, std::string>
parse_array_table_key(location& loc)
{
    if(auto token = lex_array_table::invoke(loc))
    {
        location inner_loc(loc.name(), token.unwrap().str());

        const auto open = lex_array_table_open::invoke(inner_loc);
        if(!open || inner_loc.iter() == inner_loc.end())
        {
            throw internal_error(format_underline(
                "toml::parse_array_table_key: no `[[`",
                {{source_location(inner_loc), "should be `[[`"}}),
                source_location(inner_loc));
        }
        lex_ws::invoke(inner_loc);
        const auto keys = parse_key(inner_loc);
        if(!keys)
        {
            throw internal_error(format_underline(
                "toml::parse_array_table_key: invalid key",
                {{source_location(inner_loc), "not a key"}}),
                source_location(inner_loc));
        }
        lex_ws::invoke(inner_loc);
        const auto close = lex_array_table_close::invoke(inner_loc);
        if(!close)
        {
            throw internal_error(format_underline(
                "toml::parse_array_table_key: no `]]`",
                {{source_location(inner_loc), "should be `]]`"}}),
                source_location(inner_loc));
        }

        // after [[table.key]], newline or EOF(empty table) required.
        if(loc.iter() != loc.end())
        {
            using lex_newline_after_table_key =
                sequence<maybe<lex_ws>, maybe<lex_comment>, lex_newline>;
            const auto nl = lex_newline_after_table_key::invoke(loc);
            if(!nl)
            {
                throw syntax_error(format_underline("toml::"
                    "parse_array_table_key: newline required after [[table.key]]",
                    {{source_location(loc), "expected newline"}}),
                    source_location(loc));
            }
        }
        return ok(std::make_pair(keys.unwrap().first, token.unwrap()));
    }
    else
    {
        return err(format_underline("toml::parse_array_table_key: "
            "not a valid table key", {{source_location(loc), "here"}}));
    }
}

// parse table body (key-value pairs until the iter hits the next [tablekey])
template<typename Value>
result<typename Value::table_type, std::string>
parse_ml_table(location& loc)
{
    using value_type = Value;
    using table_type = typename value_type::table_type;

    const auto first = loc.iter();
    if(first == loc.end())
    {
        return ok(table_type{});
    }

    // XXX at lest one newline is needed.
    using skip_line = repeat<
        sequence<maybe<lex_ws>, maybe<lex_comment>, lex_newline>, at_least<1>>;
    skip_line::invoke(loc);
    lex_ws::invoke(loc);

    table_type tab;
    while(loc.iter() != loc.end())
    {
        lex_ws::invoke(loc);
        const auto before = loc.iter();
        if(const auto tmp = parse_array_table_key(loc)) // next table found
        {
            loc.reset(before);
            return ok(tab);
        }
        if(const auto tmp = parse_table_key(loc)) // next table found
        {
            loc.reset(before);
            return ok(tab);
        }

        if(const auto kv = parse_key_value_pair<value_type>(loc, 0))
        {
            const auto&              kvpair  = kv.unwrap();
            const std::vector<key>&  keys    = kvpair.first.first;
            const auto&              key_reg = kvpair.first.second;
            const value_type&        val     = kvpair.second;
            const auto inserted =
                insert_nested_key(tab, val, keys.begin(), keys.end(), key_reg);
            if(!inserted)
            {
                return err(inserted.unwrap_err());
            }
        }
        else
        {
            return err(kv.unwrap_err());
        }

        // comment lines are skipped by the above function call.
        // However, since the `skip_line` requires at least 1 newline, it fails
        // if the file ends with ws and/or comment without newline.
        // `skip_line` matches `ws? + comment? + newline`, not `ws` or `comment`
        // itself. To skip the last ws and/or comment, call lexers.
        // It does not matter if these fails, so the return value is discarded.
        lex_ws::invoke(loc);
        lex_comment::invoke(loc);

        // skip_line is (whitespace? comment? newline)_{1,}. multiple empty lines
        // and comments after the last key-value pairs are allowed.
        const auto newline = skip_line::invoke(loc);
        if(!newline && loc.iter() != loc.end())
        {
            const auto before2 = loc.iter();
            lex_ws::invoke(loc); // skip whitespace
            const auto msg = format_underline("toml::parse_table: "
                "invalid line format", {{source_location(loc), concat_to_string(
                "expected newline, but got '", show_char(*loc.iter()), "'.")}});
            loc.reset(before2);
            return err(msg);
        }

        // the skip_lines only matches with lines that includes newline.
        // to skip the last line that includes comment and/or whitespace
        // but no newline, call them one more time.
        lex_ws::invoke(loc);
        lex_comment::invoke(loc);
    }
    return ok(tab);
}

template<typename Value>
result<Value, std::string> parse_toml_file(location& loc)
{
    using value_type = Value;
    using table_type = typename value_type::table_type;

    const auto first = loc.iter();
    if(first == loc.end())
    {
        // For empty files, return an empty table with an empty region (zero-length).
        // Without the region, error messages would miss the filename.
        return ok(value_type(table_type{}, region(loc, first, first), {}));
    }

    // put the first line as a region of a file
    // Here first != loc.end(), so taking std::next is okay
    const region file(loc, first, std::next(loc.iter()));

    // The first successive comments that are separated from the first value
    // by an empty line are for a file itself.
    // ```toml
    // # this is a comment for a file.
    //
    // key = "the first value"
    // ```
    // ```toml
    // # this is a comment for "the first value".
    // key = "the first value"
    // ```
    std::vector<std::string> comments;
    using lex_first_comments = sequence<
        repeat<sequence<maybe<lex_ws>, lex_comment, lex_newline>, at_least<1>>,
        sequence<maybe<lex_ws>, lex_newline>
        >;
    if(const auto token = lex_first_comments::invoke(loc))
    {
        location inner_loc(loc.name(), token.unwrap().str());
        while(inner_loc.iter() != inner_loc.end())
        {
            maybe<lex_ws>::invoke(inner_loc); // remove ws if exists
            if(lex_newline::invoke(inner_loc))
            {
                assert(inner_loc.iter() == inner_loc.end());
                break; // empty line found.
            }
            auto com = lex_comment::invoke(inner_loc).unwrap().str();
            com.erase(com.begin()); // remove # sign
            comments.push_back(std::move(com));
            lex_newline::invoke(inner_loc);
        }
    }

    table_type data;
    // root object is also a table, but without [tablename]
    if(const auto tab = parse_ml_table<value_type>(loc))
    {
        data = std::move(tab.unwrap());
    }
    else // failed (empty table is regarded as success in parse_ml_table)
    {
        return err(tab.unwrap_err());
    }
    while(loc.iter() != loc.end())
    {
        // here, the region of [table] is regarded as the table-key because
        // the table body is normally too big and it is not so informative
        // if the first key-value pair of the table is shown in the error
        // message.
        if(const auto tabkey = parse_array_table_key(loc))
        {
            const auto tab = parse_ml_table<value_type>(loc);
            if(!tab){return err(tab.unwrap_err());}

            const auto& tk   = tabkey.unwrap();
            const auto& keys = tk.first;
            const auto& reg  = tk.second;

            const auto inserted = insert_nested_key(data,
                    value_type(tab.unwrap(), reg, reg.comments()),
                    keys.begin(), keys.end(), reg,
                    /*is_array_of_table=*/ true);
            if(!inserted) {return err(inserted.unwrap_err());}

            continue;
        }
        if(const auto tabkey = parse_table_key(loc))
        {
            const auto tab = parse_ml_table<value_type>(loc);
            if(!tab){return err(tab.unwrap_err());}

            const auto& tk   = tabkey.unwrap();
            const auto& keys = tk.first;
            const auto& reg  = tk.second;

            const auto inserted = insert_nested_key(data,
                value_type(tab.unwrap(), reg, reg.comments()),
                keys.begin(), keys.end(), reg);
            if(!inserted) {return err(inserted.unwrap_err());}

            continue;
        }
        return err(format_underline("toml::parse_toml_file: "
            "unknown line appeared", {{source_location(loc), "unknown format"}}));
    }

    return ok(Value(std::move(data), file, comments));
}

template<typename                     Comment = TOML11_DEFAULT_COMMENT_STRATEGY,
         template<typename ...> class Table   = std::unordered_map,
         template<typename ...> class Array   = std::vector>
basic_value<Comment, Table, Array>
parse(std::vector<char>& letters, const std::string& fname)
{
    using value_type = basic_value<Comment, Table, Array>;

    // append LF.
    // Although TOML does not require LF at the EOF, to make parsing logic
    // simpler, we "normalize" the content by adding LF if it does not exist.
    // It also checks if the last char is CR, to avoid changing the meaning.
    // This is not the *best* way to deal with the last character, but is a
    // simple and quick fix.
    if(!letters.empty() && letters.back() != '\n' && letters.back() != '\r')
    {
        letters.push_back('\n');
    }

    detail::location loc(std::move(fname), std::move(letters));

    // skip BOM if exists.
    // XXX component of BOM (like 0xEF) exceeds the representable range of
    // signed char, so on some (actually, most) of the environment, these cannot
    // be compared to char. However, since we are always out of luck, we need to
    // check our chars are equivalent to BOM. To do this, first we need to
    // convert char to unsigned char to guarantee the comparability.
    if(loc.source()->size() >= 3)
    {
        std::array<unsigned char, 3> BOM;
        std::memcpy(BOM.data(), loc.source()->data(), 3);
        if(BOM[0] == 0xEF && BOM[1] == 0xBB && BOM[2] == 0xBF)
        {
            loc.advance(3); // BOM found. skip.
        }
    }

    if (auto data = detail::parse_toml_file<value_type>(loc))
    {
        return std::move(data).unwrap();
    }
    else
    {
        throw syntax_error(std::move(data).unwrap_err(), source_location(loc));
    }
}

} // detail

template<typename                     Comment = TOML11_DEFAULT_COMMENT_STRATEGY,
         template<typename ...> class Table   = std::unordered_map,
         template<typename ...> class Array   = std::vector>
basic_value<Comment, Table, Array>
parse(FILE * file, const std::string& fname)
{
    const long beg = std::ftell(file);
    if (beg == -1l)
    {
        throw file_io_error(errno, "Failed to access", fname);
    }

    const int res_seekend = std::fseek(file, 0, SEEK_END);
    if (res_seekend != 0)
    {
        throw file_io_error(errno, "Failed to seek", fname);
    }

    const long end = std::ftell(file);
    if (end == -1l)
    {
        throw file_io_error(errno, "Failed to access", fname);
    }

    const auto fsize = end - beg;

    const auto res_seekbeg = std::fseek(file, beg, SEEK_SET);
    if (res_seekbeg != 0)
    {
        throw file_io_error(errno, "Failed to seek", fname);
    }

    // read whole file as a sequence of char
    assert(fsize >= 0);
    std::vector<char> letters(static_cast<std::size_t>(fsize));
    std::fread(letters.data(), sizeof(char), static_cast<std::size_t>(fsize), file);

    return detail::parse<Comment, Table, Array>(letters, fname);
}

template<typename                     Comment = TOML11_DEFAULT_COMMENT_STRATEGY,
         template<typename ...> class Table   = std::unordered_map,
         template<typename ...> class Array   = std::vector>
basic_value<Comment, Table, Array>
parse(std::istream& is, std::string fname = "unknown file")
{
    const auto beg = is.tellg();
    is.seekg(0, std::ios::end);
    const auto end = is.tellg();
    const auto fsize = end - beg;
    is.seekg(beg);

    // read whole file as a sequence of char
    assert(fsize >= 0);
    std::vector<char> letters(static_cast<std::size_t>(fsize));
    is.read(letters.data(), fsize);

    return detail::parse<Comment, Table, Array>(letters, fname);
}

template<typename                     Comment = TOML11_DEFAULT_COMMENT_STRATEGY,
         template<typename ...> class Table   = std::unordered_map,
         template<typename ...> class Array   = std::vector>
basic_value<Comment, Table, Array> parse(std::string fname)
{
    std::ifstream ifs(fname, std::ios_base::binary);
    if(!ifs.good())
    {
        throw std::ios_base::failure(
                "toml::parse: Error opening file \"" + fname + "\"");
    }
    ifs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return parse<Comment, Table, Array>(ifs, std::move(fname));
}

#ifdef TOML11_HAS_STD_FILESYSTEM
// This function just forwards `parse("filename.toml")` to std::string version
// to avoid the ambiguity in overload resolution.
//
// Both std::string and std::filesystem::path are convertible from const char*.
// Without this, both parse(std::string) and parse(std::filesystem::path)
// matches to parse("filename.toml"). This breaks the existing code.
//
// This function exactly matches to the invocation with c-string.
// So this function is preferred than others and the ambiguity disappears.
template<typename                     Comment = TOML11_DEFAULT_COMMENT_STRATEGY,
         template<typename ...> class Table   = std::unordered_map,
         template<typename ...> class Array   = std::vector>
basic_value<Comment, Table, Array> parse(const char* fname)
{
    return parse<Comment, Table, Array>(std::string(fname));
}

template<typename                     Comment = TOML11_DEFAULT_COMMENT_STRATEGY,
         template<typename ...> class Table   = std::unordered_map,
         template<typename ...> class Array   = std::vector>
basic_value<Comment, Table, Array> parse(const std::filesystem::path& fpath)
{
    std::ifstream ifs(fpath, std::ios_base::binary);
    if(!ifs.good())
    {
        throw std::ios_base::failure(
                "toml::parse: Error opening file \"" + fpath.string() + "\"");
    }
    ifs.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    return parse<Comment, Table, Array>(ifs, fpath.string());
}
#endif // TOML11_HAS_STD_FILESYSTEM

} // toml
#endif// TOML11_PARSER_HPP
