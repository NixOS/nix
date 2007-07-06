#ifndef __TYPES_H
#define __TYPES_H

#include <string>
#include <list>
#include <set>
#include <map>

#include <boost/format.hpp>


namespace nix {


/* Inherit some names from other namespaces for convenience. */
using std::string;
using std::list;
using std::set;
using std::map;
using std::vector;
using boost::format;


class Error : public std::exception
{
protected:
    string err;
public:
    Error(const format & f);
    ~Error() throw () { };
    const char * what() const throw () { return err.c_str(); }
    const string & msg() const throw () { return err; }
    Error & addPrefix(const format & f);
};

class SysError : public Error
{
public:
    int errNo;
    SysError(const format & f);
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        newClass(const format & f) : superClass(f) { }; \
    };


typedef list<string> Strings;
typedef list<Strings> StringsList;
typedef set<string> StringSet;
typedef set<StringSet> SetStringSet;

/* Paths are just strings. */
typedef string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;

//state types
struct RevisionNumbersKey { Path statePath; int revision; } ;
typedef list<int> RevisionNumbers;								//the Strings (list) of StateReferences and this list are connected by position
typedef map<int, Strings> StateReferences;

//typedef std::map<int, Strings> StateReferencesKey;
//typedef std::map<int, Strings> StateReferencesKey2;


 
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
