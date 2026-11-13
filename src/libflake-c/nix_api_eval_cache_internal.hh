#pragma once

#include "nix/expr/eval-cache.hh"

struct nix_eval_cache
{
    nix::ref<nix::eval_cache::EvalCache> cache;
};

struct nix_attr_cursor
{
    nix::ref<nix::eval_cache::AttrCursor> cursor;
};
