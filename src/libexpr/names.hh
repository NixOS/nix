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
    DrvName(const string & s);
    bool matches(DrvName & n);

private:
    std::unique_ptr<std::regex> regex;
};

typedef list<DrvName> DrvNames;

int compareVersions(const string & v1, const string & v2);
DrvNames drvNamesFromArgs(const Strings & opArgs);

}
