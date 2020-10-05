#pragma once

#include "ref.hh"

#include <list>
#include <set>
#include <string>
#include <map>
#include <vector>

namespace nix {

using std::list;
using std::set;
using std::vector;
using std::string;

typedef list<string> Strings;
typedef set<string> StringSet;
typedef std::map<string, string> StringMap;

/* Paths are just strings. */

typedef string Path;
typedef list<Path> Paths;
typedef set<Path> PathSet;

typedef vector<std::pair<string, string>> Headers;

/* Helper class to run code at startup. */
template<typename T>
struct OnStartup
{
    OnStartup(T && t) { t(); }
};

}
