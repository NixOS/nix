#ifndef __TYPES_H
#define __TYPES_H

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
    BaseError(const format & f);
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
        newClass(const format & f) : superClass(f) { }; \
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

 
typedef enum { 
    lvlError,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;


}


#endif /* !__TYPES_H */
