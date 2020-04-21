#pragma once

#include <boost/format.hpp>
#include <string>
#include "ansicolor.hh"


namespace nix {


/* Inherit some names from other namespaces for convenience. */
using std::string;
using boost::format;


/* A variadic template that does nothing. Useful to call a function
   for all variadic arguments but ignoring the result. */
struct nop { template<typename... T> nop(T...) {} };


struct FormatOrString
{
    string s;
    FormatOrString(const string & s) : s(s) { };
    template<class F>
    FormatOrString(const F & f) : s(f.str()) { };
    FormatOrString(const char * s) : s(s) { };
};


/* A helper for formatting strings. ‘fmt(format, a_0, ..., a_n)’ is
   equivalent to ‘boost::format(format) % a_0 % ... %
   ... a_n’. However, ‘fmt(s)’ is equivalent to ‘s’ (so no %-expansion
   takes place). */

template<class F>
inline void formatHelper(F & f)
{
}

template<class F, typename T, typename... Args>
inline void formatHelper(F & f, const T & x, const Args & ... args)
{
    formatHelper(f % x, args...);
}

inline std::string fmt(const std::string & s)
{
    return s;
}

inline std::string fmt(const char * s)
{
    return s;
}

inline std::string fmt(const FormatOrString & fs)
{
    return fs.s;
}

template<typename... Args>
inline std::string fmt(const std::string & fs, const Args & ... args)
{
    boost::format f(fs);
    f.exceptions(boost::io::all_error_bits ^ boost::io::too_many_args_bit);
    formatHelper(f, args...);
    return f.str();
}

// -----------------------------------------------------------------------------
// format function for hints in errors.  same as fmt, except templated values
// are always in yellow.

template <class T>
struct yellowify
{
    yellowify(T &s) : value(s) {}
    T &value;
};

template <class T>
std::ostream& operator<<(std::ostream &out, const yellowify<T> &y)
{
    return out << ANSI_YELLOW << y.value << ANSI_NORMAL;
}

class hintformat
{
public:
    hintformat(const string &format) :fmt(format)
    {
        fmt.exceptions(boost::io::all_error_bits ^ boost::io::too_many_args_bit);
    }
    hintformat(const hintformat &hf)
    : fmt(hf.fmt)
    {}
    template<class T>
    hintformat& operator%(const T &value)
    {
        fmt % yellowify(value);
        return *this;
    }


    std::string str() const
    {
        return fmt.str();
    }

    template <typename U>
    friend class AddHint;
private:
    format fmt;
};

std::ostream& operator<<(std::ostream &os, const hintformat &hf);

template<typename... Args>
inline hintformat hintfmt(const std::string & fs, const Args & ... args)
{
    hintformat f(fs);
    formatHelper(f, args...);
    return f;
}

}
