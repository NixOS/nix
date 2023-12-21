#pragma once
///@file

#include "ref.hh"
#include "git-utils.hh"

namespace nix::fetchers {

struct TarballInfo
{
    Hash treeHash;
    time_t lastModified;
};

ref<GitRepo> getTarballCache();

}
