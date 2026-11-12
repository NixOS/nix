#pragma once
///@file

#include <string>
#include <string_view>

namespace nix {

/**
 * A `boost::format` format string that is a compile-time constant.
 *
 * The converting constructor is `consteval`. A string that is computed at
 * run time does not convert, so the compiler rejects code that uses data as a
 * format string. Such data can contain `%`, which `boost::format` interprets.
 *
 * A function that forwards a format string must take this type itself. The
 * conversion is checked where the literal is written, and a function
 * parameter is not a constant expression.
 *
 * To pick one of several literals, convert each one, as in
 * `cond ? FormatString("a %s") : FormatString("b %s")`. A conditional
 * expression on bare literals gives a `const char *`, which does not convert.
 *
 * For a format string that is assembled at run time, use
 * `HintFmt::fromFormatString`.
 */
class FormatString
{
    std::string_view s;

public:
    /**
     * The length comes from the array type, so a literal can contain `\0`.
     */
    template<size_t N>
    consteval FormatString(const char (&s)[N])
        : s(s, N - 1)
    {
    }

    consteval FormatString(std::string_view s)
        : s(s)
    {
    }

    std::string str() const
    {
        return std::string(s);
    }
};

} // namespace nix
