#pragma once

#include <memory>

#include "types.hh"
#include "versions.hh"
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

DrvNames drvNamesFromArgs(const Strings & opArgs);

}
