#pragma once

#include "derivations.hh"
#include "store-api.hh"

namespace nix {

struct Package {
    Path path;
    bool active;
    int priority;
    Package(const Path & path, bool active, int priority) : path{path}, active{active}, priority{priority} {}
};

typedef std::vector<Package> Packages;

void buildProfile(const Path & out, Packages && pkgs);

void builtinBuildenv(const BasicDerivation & drv);

}
