#ifndef NIX_API_STORE_INTERNAL_H
#define NIX_API_STORE_INTERNAL_H
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/store/path-info.hh"

extern "C" {

struct Store
{
    nix::ref<nix::Store> ptr;
};

struct StorePath
{
    nix::StorePath path;
};

struct nix_derivation
{
    nix::Derivation drv;
};

struct nix_path_info
{
    nix::ref<const nix::ValidPathInfo> info;
};

} // extern "C"

#endif
