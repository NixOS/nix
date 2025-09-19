#ifndef NIX_API_STORE_INTERNAL_H
#define NIX_API_STORE_INTERNAL_H
#include "nix/store/derivations.hh"
#include "nix/store/store-api.hh"

struct Store
{
    nix::ref<nix::Store> ptr;
};

struct StorePath
{
    nix::StorePath path;
};

struct Derivation
{
    nix::Derivation drv;
};

struct DerivationOutput
{
    nix::DerivationOutput drv_out;
};

#endif
