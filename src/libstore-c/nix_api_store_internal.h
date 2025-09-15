#ifndef NIX_API_STORE_INTERNAL_H
#define NIX_API_STORE_INTERNAL_H
#include "nix/store/store-api.hh"

extern "C" {

struct Store
{
    nix::ref<nix::Store> ptr;
};

struct StorePath
{
    nix::StorePath path;
};

} // extern "C"

#endif
