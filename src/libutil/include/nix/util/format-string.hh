#pragma once
///@file

#include "nix/util/array-from-string-literal.hh"

#include <algorithm>
#include <string>
#include <string_view>
#include <type_traits>

namespace nix {

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
 * Boost also has tabulation (`%t`, `%Tc`), `%n`, a `*` width or precision,
 * and Microsoft length modifiers. Nix does not use them, so this function
 * rejects them. A `*` takes an argument in printf but none in boost. On every
 * string this function accepts, the count is `boost::format::expected_args()`.
 * With numbered directives, the count is the highest N. Boost rejects a mix
 * of numbered and sequential directives, and so does this function.
 *
 * It rejects a string with a `throw`, which ends constant evaluation, so the
 * compiler error quotes the `throw` and its reason.
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
            throw "format string ends in a single %";
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
                throw "format string has an unsupported directive";
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
                throw "format string has an unsupported directive";
            ++i;
            if (bars) {
                if (i == s.size() || s[i] != '|')
                    throw "format string has an unsupported directive";
                ++i;
            }
        }
        if (numbered)
            highestNumbered = std::max(highestNumbered, number);
        else
            ++sequential;
    }
    if (sequential && highestNumbered)
        throw "format string mixes numbered and sequential directives";
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
 * exactly `nArgs` arguments. Without arguments, the string is printed
 * literally, so the constructor rejects a `%`: a directive there means a
 * forgotten argument, and a `%%` would print as two signs. To print a `%`,
 * pass it as an argument, as in `warn("%s", "100%")`.
 *
 * A function that forwards a format string must take this type itself. The
 * conversion is checked where the literal is written, and a function
 * parameter is not a constant expression. Such a function takes
 * `FormatString<N>`, where N is the number of its format arguments, so the
 * arguments determine `nArgs`.
 *
 * To pick one of several literals, convert each one with `_fmt`, as in
 * `cond ? "a %s"_fmt : "b %s"_fmt`. A conditional is not a constant expression
 * unless its condition is, so its result does not convert.
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
        if (nArgs == 0) {
            if (s.find('%') != std::string_view::npos)
                throw "a message without arguments contains %";
        } else if (formatArgumentCount(s) != nArgs)
            throw "format string has the wrong argument count";
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
 * @return a FormatString<N> where N is determined by the number of
 *         directives in the string literal.
 */
template<ArrayNoNullAdaptor literal>
consteval auto operator""_fmt()
{
    constexpr std::string_view s(literal.data.data(), literal.data.size());
    return FormatString<formatArgumentCount(s)>(s);
}

/**
 * A message that is not a `char` array, such as a `std::string`, a
 * `std::string_view` or a `const char *`. Pass a `char` buffer as a
 * `std::string_view`.
 *
 * An overload that prints such a message literally takes this, so that a
 * literal goes to the `FormatString` overload beside it, which checks it.
 */
template<typename S>
concept RuntimeMessage = std::is_convertible_v<const S &, std::string_view> && !std::is_array_v<S>;

} // namespace nix
