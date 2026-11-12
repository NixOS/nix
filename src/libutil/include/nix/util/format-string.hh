#pragma once
///@file

#include "nix/util/array-from-string-literal.hh"

#include <algorithm>
#include <string>
#include <string_view>

namespace nix {

/**
 * `formatArgumentCount` calls these undefined, non-`constexpr` functions to
 * end constant evaluation, so the compiler error names the reason.
 */
void formatStringMixesNumberedAndSequentialDirectives();
void formatStringHasUnsupportedDirective();
void formatStringEndsInPercent();
void formatStringHasWrongArgumentCount();

/**
 * The number of arguments that a `boost::format` format string consumes.
 *
 * Accepts this subset of boost's syntax, which follows
 * `parse_printf_directive` in `boost/format/parsing.hpp`:
 *
 * - `%%`
 * - `%N%`
 * - `%spec` and `%N$spec`
 * - `%|spec|` and `%|N$spec|`, where the type in spec is optional
 *
 * N starts with 1 to 9, in `%N%` and in `N$`. spec is
 * `[flags][width][.precision][length]type`: flags from `-=_ +0#'`, width and
 * precision in digits, length from `hljzL`, and type from
 * `bdiuoxXpaAeEfFgGcCsS`. Flags come before the width, as boost reads digits
 * 1 to 9 after `%` as N or as the width.
 *
 * Boost also has tabulation (`%t`, `%Tc`) and `%n`, which take no argument, a
 * `*` width or precision, which it ignores, and Microsoft length modifiers.
 * This function rejects those, so on every string it accepts, the count is
 * `boost::format::expected_args()`. With numbered directives, the count is
 * the highest N. Boost rejects a mix of numbered and sequential directives,
 * and so does this function.
 */
consteval size_t formatArgumentCount(std::string_view s)
{
    auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    size_t sequential = 0, highestNumbered = 0;
    size_t i = 0;
    auto skip = [&](std::string_view chars) {
        while (i < s.size() && chars.find(s[i]) != std::string_view::npos)
            ++i;
    };
    while (i < s.size()) {
        if (s[i++] != '%')
            continue;
        if (i == s.size())
            formatStringEndsInPercent();
        if (s[i] == '%') {
            ++i;
            continue;
        }
        bool bars = s[i] == '|';
        if (bars)
            ++i;
        /* Boost reads a leading 0 as a flag, and other digits as N or as the
           width, depending on what follows them. */
        size_t digitsStart = i, number = 0;
        if (i < s.size() && s[i] != '0')
            while (i < s.size() && isDigit(s[i]))
                number = number * 10 + (s[i++] - '0');
        bool numbered = false, widthDone = false;
        if (i > digitsStart && i < s.size() && s[i] == '%') {
            if (bars)
                formatStringHasUnsupportedDirective();
            highestNumbered = std::max(highestNumbered, number);
            ++i;
            continue;
        } else if (i > digitsStart && i < s.size() && s[i] == '$') {
            numbered = true;
            ++i;
        } else
            widthDone = i > digitsStart;
        if (!widthDone) {
            skip("-=_ +0#'");
            skip("0123456789");
        }
        if (i < s.size() && s[i] == '.') {
            ++i;
            skip("0123456789");
        }
        skip("hljzL");
        if (bars && i < s.size() && s[i] == '|')
            ++i;
        else {
            if (i == s.size() || std::string_view("bdiuoxXpaAeEfFgGcCsS").find(s[i]) == std::string_view::npos)
                formatStringHasUnsupportedDirective();
            ++i;
            if (bars) {
                if (i == s.size() || s[i] != '|')
                    formatStringHasUnsupportedDirective();
                ++i;
            }
        }
        if (numbered)
            highestNumbered = std::max(highestNumbered, number);
        else
            ++sequential;
    }
    if (sequential && highestNumbered)
        formatStringMixesNumberedAndSequentialDirectives();
    return sequential + highestNumbered;
}

/**
 * A `boost::format` format string that is a compile-time constant, for
 * `nArgs` arguments.
 *
 * The converting constructor is `consteval`. A string that is computed at
 * run time does not convert, so the compiler rejects code that uses data as a
 * format string. Such data can contain `%`, which `boost::format` interprets.
 * The constructor also rejects a format string whose directives do not take
 * exactly `nArgs` arguments, except for `nArgs` 0: without arguments, the
 * string is printed literally.
 *
 * A function that forwards a format string must take this type itself. The
 * conversion is checked where the literal is written, and a function
 * parameter is not a constant expression. Such a function takes
 * `FormatString<N>`, where N is the number of its format arguments, so the
 * arguments determine `nArgs`.
 *
 * To pick one of several literals, convert each one with `_fmt`, as in
 * `cond ? "a %s"_fmt : "b %s"_fmt`. A conditional expression on bare literals
 * gives a `const char *`, which does not convert.
 *
 * For a format string that is assembled at run time, use
 * `HintFmt::fromFormatString`.
 */
template<size_t nArgs>
class FormatString
{
    std::string_view s;

    consteval void check()
    {
        if (nArgs != 0 && formatArgumentCount(s) != nArgs)
            formatStringHasWrongArgumentCount();
    }

public:
    /**
     * The length comes from the array type, so a literal can contain `\0`.
     */
    template<size_t N>
    consteval FormatString(const char (&s)[N])
        : s(s, N - 1)
    {
        check();
    }

    consteval FormatString(std::string_view s)
        : s(s)
    {
        check();
    }

    std::string str() const
    {
        return std::string(s);
    }
};

/**
 * A literal as a `FormatString` for as many arguments as its directives take.
 *
 * The literal is a template argument, so its directives determine the type.
 * The two literals of a conditional must take the same number of arguments.
 */
template<ArrayNoNullAdaptor literal>
consteval auto operator""_fmt()
{
    constexpr std::string_view s(literal.data.data(), literal.data.size());
    return FormatString<formatArgumentCount(s)>(s);
}

} // namespace nix
