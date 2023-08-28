#ifndef NIX_API_EXPR_INTERNAL_H
#define NIX_API_EXPR_INTERNAL_H

#include "eval.hh"
#include "attr-set.hh"

struct State
{
    nix::EvalState state;
};

struct BindingsBuilder
{
    nix::BindingsBuilder builder;
};

#endif // NIX_API_EXPR_INTERNAL_H
