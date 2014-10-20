#pragma once

#include "config.h"

#include <string>
#include <list>
#include <set>

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


struct FormatOrString
{
    string s;
    FormatOrString(const string & s) : s(s) { };
    FormatOrString(const format & f) : s(f.str()) { };
    FormatOrString(const char * s) : s(s) { };
};


/* BaseError should generally not be caught, as it has Interrupted as
   a subclass. Catch Error instead. */
class BaseError : public std::exception
{
protected:
    string prefix_; // used for location traces etc.
    string err;
public:
    unsigned int status; // exit status
    BaseError(const FormatOrString & fs, unsigned int status = 1);
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
        newClass(const FormatOrString & fs, unsigned int status = 1) : superClass(fs, status) { }; \
    };

MakeError(Error, BaseError)

class SysError : public Error
{
public:
    int errNo;
    SysError(const FormatOrString & fs);
};


typedef list<string> Strings;
typedef set<string> StringSet;


/* Paths are just strings. */
typedef string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;


typedef enum {
    lvlError = 0,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;


}
