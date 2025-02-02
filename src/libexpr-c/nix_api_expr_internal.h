#ifndef NIX_API_EXPR_INTERNAL_H
#define NIX_API_EXPR_INTERNAL_H

#include "fetch-settings.hh"
#include "eval.hh"
#include "eval-settings.hh"
#include "attr-set.hh"
#include "nix_api_value.h"
#include "search-path.hh"

struct nix_eval_state_builder
{
    nix::ref<nix::Store> store;
    nix::EvalSettings settings;
    nix::fetchers::Settings fetchSettings;
    nix::LookupPath lookupPath;
    // TODO: make an EvalSettings setting own this instead?
    bool readOnlyMode;
};

struct EvalState
{
    nix::fetchers::Settings fetchSettings;
    nix::EvalSettings settings;
    nix::EvalState state;
};

struct BindingsBuilder
{
    nix::BindingsBuilder builder;
};

struct ListBuilder
{
    nix::ListBuilder builder;
};

struct nix_value
{
    nix::Value value;
};

struct nix_string_return
{
    std::string str;
};

struct nix_printer
{
    std::ostream & s;
};

struct nix_string_context
{
    nix::NixStringContext & ctx;
};

struct nix_realised_string
{
    std::string str;
    std::vector<StorePath> storePaths;
};

#endif // NIX_API_EXPR_INTERNAL_H
