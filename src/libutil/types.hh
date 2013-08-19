#pragma once

#include "config.h"

#include <string>
#include <list>
#include <set>

#include <boost/format.hpp>


namespace nix {


/* Inherit some names from other namespaces for convenience. */
using std::string;
using std::list;
using std::set;
using std::vector;
using boost::format;


/* BaseError should generally not be caught, as it has Interrupted as
   a subclass. Catch Error instead. */
class BaseError : public std::exception 
{
protected:
    string prefix_; // used for location traces etc.
    string err;
public:
    unsigned int status; // exit status
    BaseError(const format & f, unsigned int status = 1);
    ~BaseError() throw () { };
    const char * what() const throw () { return err.c_str(); }
    const string & msg() const throw () { return err; }
    const string & prefix() const throw () { return prefix_; }
    BaseError & addPrefix(const format & f);
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        newClass(const format & f, unsigned int status = 1) : superClass(f, status) { }; \
    };

MakeError(Error, BaseError)

class SysError : public Error
{
public:
    int errNo;
    SysError(const format & f);
};


typedef list<string> Strings;
typedef set<string> StringSet;


/* Paths are just strings. */
typedef string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;

/* croup paths */
typedef string Cgroup;
typedef list<Cgroup> Cgroups;

typedef enum { 
    lvlError = 0,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;


}
