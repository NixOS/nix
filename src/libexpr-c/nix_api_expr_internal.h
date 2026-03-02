#ifndef NIX_API_EXPR_INTERNAL_H
#define NIX_API_EXPR_INTERNAL_H

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
    // TODO: make an EvalSettings setting own this instead?
    bool readOnlyMode;
};

struct EvalState
{
    nix::fetchers::Settings fetchSettings;
    nix::EvalSettings settings;
    std::shared_ptr<nix::EvalState> statePtr;
    nix::EvalState & state;
};

/**
 * Create a non-owning EvalState wrapper around a nix::EvalState.
 *
 * Useful in primop callbacks and external-value methods where
 * we receive a raw nix::EvalState& (e.g. from a C++ plugin)
 * and need to hand a valid EvalState* to the C API callback.
 * The returned wrapper must not outlive the referenced state.
 *
 * The fetchSettings and settings fields are default-initialized
 * dummies; no C API function accesses them through EvalState*.
 */
struct EvalStateRef
{
    /// Backing storage for the EvalSettings::readOnlyMode reference.
    /// EvalSettings takes bool& in its constructor; this field keeps
    /// that reference valid.  The settings object itself is never
    /// accessed — C API functions only use wrapper.state.
    ///
    /// We intentionally do NOT read inner.settings.readOnlyMode here
    /// because the EvalSettings::readOnlyMode reference may dangle
    /// after the builder that created it has been freed.
    bool readOnlyMode = false;
    EvalState wrapper;

    EvalStateRef(nix::EvalState & inner)
        : wrapper{.settings = nix::EvalSettings{readOnlyMode}, .statePtr = nullptr, .state = inner}
    {
    }
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

#endif // NIX_API_EXPR_INTERNAL_H
