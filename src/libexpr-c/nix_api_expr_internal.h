#ifndef NIX_API_EXPR_INTERNAL_H
#define NIX_API_EXPR_INTERNAL_H

#include <memory>
#include <stdexcept>

#include "nix/fetchers/fetch-settings.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include "nix/expr/attr-set.hh"
#include "nix_api_value.h"
#include "nix/expr/search-path.hh"

extern "C" {

struct nix_eval_state_builder
{
    nix::ref<nix::Store> store;
    nix::EvalSettings settings;
    nix::fetchers::Settings fetchSettings;
    nix::LookupPath lookupPath;
    nix::ref<bool> readOnlyMode;
};

struct EvalState
{
    nix::EvalState & state;
    // Owned resources; null for temporary wrappers created in C API callbacks.
    std::unique_ptr<nix::fetchers::Settings> ownedFetchSettings;
    std::unique_ptr<nix::EvalSettings> ownedSettings;
    std::shared_ptr<nix::EvalState> ownedState;
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
    nix::Value * value;
    /**
     * As we move to a managed heap, we need EvalMemory in more places. Ideally, we would take in EvalState or
     * EvalMemory as an argument when we need it, but we don't want to make changes to the stable C api, so we stuff it
     * into the nix_value that will get passed in to the relevant functions.
     */
    nix::EvalMemory * mem;
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

} // extern "C"

// Shared helpers for validating nix_value [in] parameters across libexpr-c translation units.
inline const nix::Value & check_value_not_null(const nix_value * value)
{
    if (!value || !value->value)
        throw std::runtime_error("nix_value is null");
    return *value->value;
}

inline nix::Value & check_value_not_null(nix_value * value)
{
    if (!value || !value->value)
        throw std::runtime_error("nix_value is null");
    return *value->value;
}

inline const nix::Value & check_value_in(const nix_value * value)
{
    auto & v = check_value_not_null(value);
    if (!v.isValid())
        throw std::runtime_error("Uninitialized nix_value");
    return v;
}

inline nix::Value & check_value_in(nix_value * value)
{
    auto & v = check_value_not_null(value);
    if (!v.isValid())
        throw std::runtime_error("Uninitialized nix_value");
    return v;
}

#endif // NIX_API_EXPR_INTERNAL_H
