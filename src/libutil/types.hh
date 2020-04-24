#pragma once


#include "ref.hh"

#include <list>
#include <set>
#include <map>


namespace nix {

using std::list;
using std::set;
using std::vector;

typedef list<std::string> Strings;
typedef set<std::string> StringSet;
typedef std::map<std::string, std::string> StringMap;

/* Paths are just strings. */

typedef std::string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;


}
