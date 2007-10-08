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


/* BaseError should generally not be caught, as it has Interrupted as
   a subclass. Catch Error instead. */
class BaseError : public std::exception 
{
protected:
    string err;
public:
    BaseError(const format & f);
    ~BaseError() throw () { };
    const char * what() const throw () { return err.c_str(); }
    const string & msg() const throw () { return err; }
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
typedef list<Strings> StringsList;
typedef set<string> StringSet;
typedef set<StringSet> SetStringSet;

/* Paths are just strings. */
typedef string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;

//state types
typedef vector<int> IntVector;								//the Strings (list) of StateReferences and this list are connected by position		//TODO
typedef vector<unsigned int> UnsignedIntVector;
struct RevisionInfo
{ 
	string comment;
	unsigned int timestamp;
};
typedef map<unsigned int, RevisionInfo> RevisionInfos;
typedef map<Path, unsigned int> Snapshots;					//Automatically sorted on Path :)
typedef map<Path, Snapshots> RevisionClosure;
typedef map<Path, unsigned int> RevisionClosureTS;			//Paht with a timestamp about when the revision was made.
typedef map<int, Strings> StateReferences;

 
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
