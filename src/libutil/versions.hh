#pragma once

#include <memory>

#include "types.hh"
#include <regex>

namespace nix {

string nextComponent(string::const_iterator & p,
    const string::const_iterator end);
int compareVersions(const string & v1, const string & v2);

}
