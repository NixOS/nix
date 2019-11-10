#pragma once

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#ifdef _MSC_VER
#undef min
#undef max
#endif
#endif

#include "ref.hh"

#include <string>
#include <list>
#include <set>
#include <memory>
#include <map>

#include <boost/format.hpp>

/* Before 4.7, gcc's std::exception uses empty throw() specifiers for
 * its (virtual) destructor and what() in c++11 mode, in violation of spec
 */
#ifdef __GNUC__
#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
#define EXCEPTION_NEEDS_THROW_SPEC
#endif
#endif


namespace nix {


/* Inherit some names from other namespaces for convenience. */
using std::string;
using std::list;
using std::set;
using std::vector;
using boost::format;


/* A variadic template that does nothing. Useful to call a function
   for all variadic arguments but ignoring the result. */
struct nop { template<typename... T> nop(T...) {} };

struct FormatOrString
{
    string s;
    FormatOrString(const string & s) : s(s) { };
    FormatOrString(const format & f) : s(f.str()) { };
    FormatOrString(const char * s) : s(s) { };
};


/* A helper for formatting strings. ‘fmt(format, a_0, ..., a_n)’ is
   equivalent to ‘boost::format(format) % a_0 % ... %
   ... a_n’. However, ‘fmt(s)’ is equivalent to ‘s’ (so no %-expansion
   takes place). */

inline void formatHelper(boost::format & f)
{
}

template<typename T, typename... Args>
inline void formatHelper(boost::format & f, T x, Args... args)
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
inline std::string fmt(const std::string & fs, Args... args)
{
    boost::format f(fs);
    f.exceptions(boost::io::all_error_bits ^ boost::io::too_many_args_bit);
    formatHelper(f, args...);
    return f.str();
}


/* BaseError should generally not be caught, as it has Interrupted as
   a subclass. Catch Error instead. */
class BaseError : public std::exception
{
protected:
    string prefix_; // used for location traces etc.
    string err;
public:
    unsigned int status = 1; // exit status

    template<typename... Args>
    BaseError(unsigned int status, Args... args)
        : err(fmt(args...))
        , status(status)
    {
    }

    template<typename... Args>
    BaseError(Args... args)
        : err(fmt(args...))
    {
    }

#ifdef EXCEPTION_NEEDS_THROW_SPEC
    ~BaseError() throw () { };
    const char * what() const throw () { return err.c_str(); }
#else
    const char * what() const noexcept { return err.c_str(); }
#endif

    const string & msg() const { return err; }
    const string & prefix() const { return prefix_; }
    BaseError & addPrefix(const FormatOrString & fs);
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        using superClass::superClass;                   \
    }

MakeError(Error, BaseError);

// to use in catch-blocks
class SysError : public Error {
protected:
    SysError(const std::string & msg) : Error(msg) { }
};

// to throw
class PosixError : public SysError
{
public:
    int errNo;

    template<typename... Args>
    PosixError(Args... args)
        : SysError(addErrno(fmt(args...)))
    { }

private:

    std::string addErrno(const std::string & s);
};

#ifdef _WIN32
// to throw
class WinError : public SysError
{
public:
    DWORD lastError;

    template<typename... Args>
    WinError(Args... args)
        : SysError(addLastError(fmt(args...)))
    { }

private:

    std::string addLastError(const std::string & s);
};
#endif


typedef list<string> Strings;
typedef set<string> StringSet;
typedef std::map<std::string, std::string> StringMap;


/* Paths are just strings. */
typedef string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;


#ifdef _WIN32
std::ostream & operator << (std::ostream & os, const std::wstring & path) = delete;
std::string to_bytes(const std::wstring & path);
std::wstring from_bytes(const std::string & s);
#endif


}
