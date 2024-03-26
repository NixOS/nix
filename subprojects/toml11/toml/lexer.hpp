//     Copyright Toru Niina 2017.
// Distributed under the MIT License.
#ifndef TOML11_LEXER_HPP
#define TOML11_LEXER_HPP
#include <istream>
#include <sstream>
#include <stdexcept>

#include "combinator.hpp"

namespace toml
{
namespace detail
{

// these scans contents from current location in a container of char
// and extract a region that matches their own pattern.
// to see the implementation of each component, see combinator.hpp.

using lex_wschar  = either<character<' '>, character<'\t'>>;
using lex_ws      = repeat<lex_wschar, at_least<1>>;
using lex_newline = either<character<'\n'>,
                           sequence<character<'\r'>, character<'\n'>>>;
using lex_lower   = in_range<'a', 'z'>;
using lex_upper   = in_range<'A', 'Z'>;
using lex_alpha   = either<lex_lower, lex_upper>;
using lex_digit   = in_range<'0', '9'>;
using lex_nonzero = in_range<'1', '9'>;
using lex_oct_dig = in_range<'0', '7'>;
using lex_bin_dig = in_range<'0', '1'>;
using lex_hex_dig = either<lex_digit, in_range<'A', 'F'>, in_range<'a', 'f'>>;

using lex_hex_prefix = sequence<character<'0'>, character<'x'>>;
using lex_oct_prefix = sequence<character<'0'>, character<'o'>>;
using lex_bin_prefix = sequence<character<'0'>, character<'b'>>;
using lex_underscore = character<'_'>;
using lex_plus       = character<'+'>;
using lex_minus      = character<'-'>;
using lex_sign       = either<lex_plus, lex_minus>;

// digit | nonzero 1*(digit | _ digit)
using lex_unsigned_dec_int = either<sequence<lex_nonzero, repeat<
    either<lex_digit, sequence<lex_underscore, lex_digit>>, at_least<1>>>,
    lex_digit>;
// (+|-)? unsigned_dec_int
using lex_dec_int = sequence<maybe<lex_sign>, lex_unsigned_dec_int>;

// hex_prefix hex_dig *(hex_dig | _ hex_dig)
using lex_hex_int = sequence<lex_hex_prefix, sequence<lex_hex_dig, repeat<
    either<lex_hex_dig, sequence<lex_underscore, lex_hex_dig>>, unlimited>>>;
// oct_prefix oct_dig *(oct_dig | _ oct_dig)
using lex_oct_int = sequence<lex_oct_prefix, sequence<lex_oct_dig, repeat<
    either<lex_oct_dig, sequence<lex_underscore, lex_oct_dig>>, unlimited>>>;
// bin_prefix bin_dig *(bin_dig | _ bin_dig)
using lex_bin_int = sequence<lex_bin_prefix, sequence<lex_bin_dig, repeat<
    either<lex_bin_dig, sequence<lex_underscore, lex_bin_dig>>, unlimited>>>;

// (dec_int | hex_int | oct_int | bin_int)
using lex_integer = either<lex_bin_int, lex_oct_int, lex_hex_int, lex_dec_int>;

// ===========================================================================

using lex_inf = sequence<character<'i'>, character<'n'>, character<'f'>>;
using lex_nan = sequence<character<'n'>, character<'a'>, character<'n'>>;
using lex_special_float = sequence<maybe<lex_sign>, either<lex_inf, lex_nan>>;

using lex_zero_prefixable_int = sequence<lex_digit, repeat<either<lex_digit,
    sequence<lex_underscore, lex_digit>>, unlimited>>;

using lex_fractional_part = sequence<character<'.'>, lex_zero_prefixable_int>;

using lex_exponent_part   = sequence<either<character<'e'>, character<'E'>>,
        maybe<lex_sign>, lex_zero_prefixable_int>;

using lex_float = either<lex_special_float,
      sequence<lex_dec_int, either<lex_exponent_part,
      sequence<lex_fractional_part, maybe<lex_exponent_part>>>>>;

// ===========================================================================

using lex_true = sequence<character<'t'>, character<'r'>,
                          character<'u'>, character<'e'>>;
using lex_false = sequence<character<'f'>, character<'a'>, character<'l'>,
                           character<'s'>, character<'e'>>;
using lex_boolean = either<lex_true, lex_false>;

// ===========================================================================

using lex_date_fullyear = repeat<lex_digit, exactly<4>>;
using lex_date_month    = repeat<lex_digit, exactly<2>>;
using lex_date_mday     = repeat<lex_digit, exactly<2>>;
using lex_time_delim    = either<character<'T'>, character<'t'>, character<' '>>;
using lex_time_hour     = repeat<lex_digit, exactly<2>>;
using lex_time_minute   = repeat<lex_digit, exactly<2>>;
using lex_time_second   = repeat<lex_digit, exactly<2>>;
using lex_time_secfrac  = sequence<character<'.'>,
                                   repeat<lex_digit, at_least<1>>>;

using lex_time_numoffset = sequence<either<character<'+'>, character<'-'>>,
                                    sequence<lex_time_hour, character<':'>,
                                             lex_time_minute>>;
using lex_time_offset = either<character<'Z'>, character<'z'>,
                               lex_time_numoffset>;

using lex_partial_time = sequence<lex_time_hour,   character<':'>,
                                  lex_time_minute, character<':'>,
                                  lex_time_second, maybe<lex_time_secfrac>>;
using lex_full_date    = sequence<lex_date_fullyear, character<'-'>,
                                  lex_date_month,    character<'-'>,
                                  lex_date_mday>;
using lex_full_time    = sequence<lex_partial_time, lex_time_offset>;

using lex_offset_date_time = sequence<lex_full_date, lex_time_delim, lex_full_time>;
using lex_local_date_time  = sequence<lex_full_date, lex_time_delim, lex_partial_time>;
using lex_local_date       = lex_full_date;
using lex_local_time       = lex_partial_time;

// ===========================================================================

using lex_quotation_mark  = character<'"'>;
using lex_basic_unescaped = exclude<either<in_range<0x00, 0x08>, // 0x09 (tab) is allowed
                                           in_range<0x0A, 0x1F>,
                                           character<0x22>, character<0x5C>,
                                           character<0x7F>>>;

using lex_escape          = character<'\\'>;
using lex_escape_unicode_short = sequence<character<'u'>,
                                          repeat<lex_hex_dig, exactly<4>>>;
using lex_escape_unicode_long  = sequence<character<'U'>,
                                          repeat<lex_hex_dig, exactly<8>>>;
using lex_escape_seq_char = either<character<'"'>, character<'\\'>,
                                   character<'b'>, character<'f'>,
                                   character<'n'>, character<'r'>,
                                   character<'t'>,
#ifdef TOML11_USE_UNRELEASED_TOML_FEATURES
                                   character<'e'>, // ESC (0x1B)
#endif
                                   lex_escape_unicode_short,
                                   lex_escape_unicode_long
                                   >;
using lex_escaped      = sequence<lex_escape, lex_escape_seq_char>;
using lex_basic_char   = either<lex_basic_unescaped, lex_escaped>;
using lex_basic_string = sequence<lex_quotation_mark,
                                  repeat<lex_basic_char, unlimited>,
                                  lex_quotation_mark>;

// After toml post-v0.5.0, it is explicitly clarified how quotes in ml-strings
// are allowed to be used.
// After this, the following strings are *explicitly* allowed.
// - One or two `"`s in a multi-line basic string is allowed wherever it is.
// - Three consecutive `"`s in a multi-line basic string is considered as a delimiter.
// - One or two `"`s can appear just before or after the delimiter.
// ```toml
// str4 = """Here are two quotation marks: "". Simple enough."""
// str5 = """Here are three quotation marks: ""\"."""
// str6 = """Here are fifteen quotation marks: ""\"""\"""\"""\"""\"."""
// str7 = """"This," she said, "is just a pointless statement.""""
// ```
// In the current implementation (v3.3.0), it is difficult to parse `str7` in
// the above example. It is difficult to recognize `"` at the end of string body
// collectly. It will be misunderstood as a `"""` delimiter and an additional,
// invalid `"`. Like this:
// ```console
//   what():  [error] toml::parse_table: invalid line format
//  --> hoge.toml
//     |
//  13 | str7 = """"This," she said, "is just a pointless statement.""""
//     |                                                               ^- expected newline, but got '"'.
// ```
// As a quick workaround for this problem, `lex_ml_basic_string_delim` was
// split into two, `lex_ml_basic_string_open` and `lex_ml_basic_string_close`.
// `lex_ml_basic_string_open` allows only `"""`. `_close` allows 3-5 `"`s.
// In parse_ml_basic_string() function, the trailing `"`s will be attached to
// the string body.
//
using lex_ml_basic_string_delim = repeat<lex_quotation_mark, exactly<3>>;
using lex_ml_basic_string_open  = lex_ml_basic_string_delim;
using lex_ml_basic_string_close = sequence<
        repeat<lex_quotation_mark, exactly<3>>,
        maybe<lex_quotation_mark>, maybe<lex_quotation_mark>
    >;

using lex_ml_basic_unescaped    = exclude<either<in_range<0x00, 0x08>, // 0x09 is tab
                                                 in_range<0x0A, 0x1F>,
                                                 character<0x5C>, // backslash
                                                 character<0x7F>, // DEL
                                                 lex_ml_basic_string_delim>>;

using lex_ml_basic_escaped_newline = sequence<
        lex_escape, maybe<lex_ws>, lex_newline,
        repeat<either<lex_ws, lex_newline>, unlimited>>;

using lex_ml_basic_char = either<lex_ml_basic_unescaped, lex_escaped>;
using lex_ml_basic_body = repeat<either<lex_ml_basic_char, lex_newline,
                                        lex_ml_basic_escaped_newline>,
                                 unlimited>;
using lex_ml_basic_string = sequence<lex_ml_basic_string_open,
                                     lex_ml_basic_body,
                                     lex_ml_basic_string_close>;

using lex_literal_char = exclude<either<in_range<0x00, 0x08>, in_range<0x0A, 0x1F>,
                                        character<0x7F>, character<0x27>>>;
using lex_apostrophe = character<'\''>;
using lex_literal_string = sequence<lex_apostrophe,
                                    repeat<lex_literal_char, unlimited>,
                                    lex_apostrophe>;

// the same reason as above.
using lex_ml_literal_string_delim = repeat<lex_apostrophe, exactly<3>>;
using lex_ml_literal_string_open  = lex_ml_literal_string_delim;
using lex_ml_literal_string_close = sequence<
        repeat<lex_apostrophe, exactly<3>>,
        maybe<lex_apostrophe>, maybe<lex_apostrophe>
    >;

using lex_ml_literal_char = exclude<either<in_range<0x00, 0x08>,
                                           in_range<0x0A, 0x1F>,
                                           character<0x7F>,
                                           lex_ml_literal_string_delim>>;
using lex_ml_literal_body = repeat<either<lex_ml_literal_char, lex_newline>,
                                   unlimited>;
using lex_ml_literal_string = sequence<lex_ml_literal_string_open,
                                       lex_ml_literal_body,
                                       lex_ml_literal_string_close>;

using lex_string = either<lex_ml_basic_string,   lex_basic_string,
                          lex_ml_literal_string, lex_literal_string>;

// ===========================================================================
using lex_dot_sep = sequence<maybe<lex_ws>, character<'.'>, maybe<lex_ws>>;

using lex_unquoted_key = repeat<either<lex_alpha, lex_digit,
                                       character<'-'>, character<'_'>>,
                                at_least<1>>;
using lex_quoted_key = either<lex_basic_string, lex_literal_string>;
using lex_simple_key = either<lex_unquoted_key, lex_quoted_key>;
using lex_dotted_key = sequence<lex_simple_key,
                                repeat<sequence<lex_dot_sep, lex_simple_key>,
                                       at_least<1>
                                       >
                                >;
using lex_key = either<lex_dotted_key, lex_simple_key>;

using lex_keyval_sep = sequence<maybe<lex_ws>,
                                character<'='>,
                                maybe<lex_ws>>;

using lex_std_table_open  = character<'['>;
using lex_std_table_close = character<']'>;
using lex_std_table       = sequence<lex_std_table_open,
                                     maybe<lex_ws>,
                                     lex_key,
                                     maybe<lex_ws>,
                                     lex_std_table_close>;

using lex_array_table_open  = sequence<lex_std_table_open,  lex_std_table_open>;
using lex_array_table_close = sequence<lex_std_table_close, lex_std_table_close>;
using lex_array_table       = sequence<lex_array_table_open,
                                       maybe<lex_ws>,
                                       lex_key,
                                       maybe<lex_ws>,
                                       lex_array_table_close>;

using lex_utf8_1byte = in_range<0x00, 0x7F>;
using lex_utf8_2byte = sequence<
        in_range<'\xC2', '\xDF'>,
        in_range<'\x80', '\xBF'>
    >;
using lex_utf8_3byte = sequence<either<
        sequence<character<'\xE0'>, in_range<'\xA0', '\xBF'>>,
        sequence<in_range<'\xE1', '\xEC'>, in_range<'\x80', '\xBF'>>,
        sequence<character<'\xED'>, in_range<'\x80', '\x9F'>>,
        sequence<in_range<'\xEE', '\xEF'>, in_range<'\x80', '\xBF'>>
    >, in_range<'\x80', '\xBF'>>;
using lex_utf8_4byte = sequence<either<
        sequence<character<'\xF0'>, in_range<'\x90', '\xBF'>>,
        sequence<in_range<'\xF1', '\xF3'>, in_range<'\x80', '\xBF'>>,
        sequence<character<'\xF4'>, in_range<'\x80', '\x8F'>>
    >, in_range<'\x80', '\xBF'>, in_range<'\x80', '\xBF'>>;
using lex_utf8_code = either<
        lex_utf8_1byte,
        lex_utf8_2byte,
        lex_utf8_3byte,
        lex_utf8_4byte
    >;

using lex_comment_start_symbol = character<'#'>;
using lex_non_eol_ascii = either<character<0x09>, in_range<0x20, 0x7E>>;
using lex_comment = sequence<lex_comment_start_symbol, repeat<either<
    lex_non_eol_ascii, lex_utf8_2byte, lex_utf8_3byte, lex_utf8_4byte>, unlimited>>;

} // detail
} // toml
#endif // TOML_LEXER_HPP
