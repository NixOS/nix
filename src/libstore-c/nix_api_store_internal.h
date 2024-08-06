#ifndef NIX_API_STORE_INTERNAL_H
#define NIX_API_STORE_INTERNAL_H
#include "store-api.hh"

struct Store
{
    nix::ref<nix::Store> ptr;
};

struct StorePath
{
    nix::StorePath path;
};

#endif
