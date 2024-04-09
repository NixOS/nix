#pragma once
///@file

#include <boost/format.hpp>
#include <string>
#include "ansicolor.hh"


namespace nix {

/**
 * A helper for writing `boost::format` expressions.
 *
 * These are equivalent:
 *
 * ```
 * formatHelper(formatter, a_0, ..., a_n)
 * formatter % a_0 % ... % a_n
 * ```
 *
 * With a single argument, `formatHelper(s)` is a no-op.
 */
template<class F>
inline void formatHelper(F & f)
{ }

template<class F, typename T, typename... Args>
inline void formatHelper(F & f, const T & x, const Args & ... args)
{
    // Interpolate one argument and then recurse.
    formatHelper(f % x, args...);
}

/**
 * Set the correct exceptions for `fmt`.
 */
inline void setExceptions(boost::format & fmt)
{
    fmt.exceptions(
        boost::io::all_error_bits ^
        boost::io::too_many_args_bit ^
        boost::io::too_few_args_bit);
}

/**
 * A helper for writing a `boost::format` expression to a string.
 *
 * These are (roughly) equivalent:
 *
 * ```
 * fmt(formatString, a_0, ..., a_n)
 * (boost::format(formatString) % a_0 % ... % a_n).str()
 * ```
 *
 * However, when called with a single argument, the string is returned
 * unchanged.
 *
 * If you write code like this:
 *
 * ```
 * std::cout << boost::format(stringFromUserInput) << std::endl;
 * ```
 *
 * And `stringFromUserInput` contains formatting placeholders like `%s`, then
 * the code will crash at runtime. `fmt` helps you avoid this pitfall.
 */
inline std::string fmt(const std::string & s)
{
    return s;
}

inline std::string fmt(std::string_view s)
{
    return std::string(s);
}

inline std::string fmt(const char * s)
{
    return s;
}

template<typename... Args>
inline std::string fmt(const std::string & fs, const Args & ... args)
{
    boost::format f(fs);
    setExceptions(f);
    formatHelper(f, args...);
    return f.str();
}

/**
 * Values wrapped in this struct are printed in magenta.
 *
 * By default, arguments to `HintFmt` are printed in magenta. To avoid this,
 * either wrap the argument in `Uncolored` or add a specialization of
 * `HintFmt::operator%`.
 */
template <class T>
struct Magenta
{
    Magenta(const T &s) : value(s) {}
    const T & value;
};

template <class T>
std::ostream & operator<<(std::ostream & out, const Magenta<T> & y)
{
    return out << ANSI_WARNING << y.value << ANSI_NORMAL;
}

/**
 * Values wrapped in this class are printed without coloring.
 *
 * By default, arguments to `HintFmt` are printed in magenta (see `Magenta`).
 */
template <class T>
struct Uncolored
{
    Uncolored(const T & s) : value(s) {}
    const T & value;
};

template <class T>
std::ostream & operator<<(std::ostream & out, const Uncolored<T> & y)
{
    return out << ANSI_NORMAL << y.value;
}

/**
 * A wrapper around `boost::format` which colors interpolated arguments in
 * magenta by default.
 */
class HintFmt
{
private:
    boost::format fmt;

public:
    /**
     * Format the given string literally, without interpolating format
     * placeholders.
     */
    HintFmt(const std::string & literal)
        : HintFmt("%s", Uncolored(literal))
    { }

    static HintFmt fromFormatString(const std::string & format) {
        return HintFmt(boost::format(format));
    }

    /**
     * Interpolate the given arguments into the format string.
     */
    template<typename... Args>
    HintFmt(const std::string & format, const Args & ... args)
        : HintFmt(boost::format(format), args...)
    { }

    HintFmt(const HintFmt & hf)
        : fmt(hf.fmt)
    { }

    template<typename... Args>
    HintFmt(boost::format && fmt, const Args & ... args)
        : fmt(std::move(fmt))
    {
        setExceptions(fmt);
        formatHelper(*this, args...);
    }

    template<class T>
    HintFmt & operator%(const T & value)
    {
        fmt % Magenta(value);
        return *this;
    }

    template<class T>
    HintFmt & operator%(const Uncolored<T> & value)
    {
        fmt % value.value;
        return *this;
    }

    std::string str() const
    {
        return fmt.str();
    }
};

std::ostream & operator<<(std::ostream & os, const HintFmt & hf);

}
