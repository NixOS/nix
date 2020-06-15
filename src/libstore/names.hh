#pragma once

#include <memory>

#include "types.hh"
#include <regex>

namespace nix {

struct DrvName
{
    string fullName;
    string name;
    string version;
    unsigned int hits;

    DrvName();
    DrvName(std::string_view s);
    bool matches(DrvName & n);

private:
    std::unique_ptr<std::regex> regex;
};

typedef list<DrvName> DrvNames;

string nextComponent(string::const_iterator & p,
    const string::const_iterator end);
int compareVersions(std::string_view v1, std::string_view v2);
DrvNames drvNamesFromArgs(const Strings & opArgs);

}
