#include "nix_api_flake.h"
#include "nix_api_flake_internal.hh"
#include "nix_api_util.h"
#include "nix_api_util_internal.h"
#include "nix_api_expr_internal.h"
#include "nix_api_fetchers_internal.hh"
#include "nix_api_fetchers.h"

#include "nix/flake/flake.hh"

nix_flake_settings * nix_flake_settings_new(nix_c_context * context)
{
    try {
        auto settings = nix::make_ref<nix::flake::Settings>();
        return new nix_flake_settings{settings};
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_flake_settings_free(nix_flake_settings * settings)
{
    delete settings;
}

nix_err nix_flake_settings_add_to_eval_state_builder(
    nix_c_context * context, nix_flake_settings * settings, nix_eval_state_builder * builder)
{
    try {
        settings->settings->configureEvalSettings(builder->settings);
    }
    NIXC_CATCH_ERRS
}

nix_flake_reference_parse_flags *
nix_flake_reference_parse_flags_new(nix_c_context * context, nix_flake_settings * settings)
{
    nix_clear_err(context);
    try {
        return new nix_flake_reference_parse_flags{
            .baseDirectory = std::nullopt,
        };
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_flake_reference_parse_flags_free(nix_flake_reference_parse_flags * flags)
{
    delete flags;
}

nix_err nix_flake_reference_parse_flags_set_base_directory(
    nix_c_context * context,
    nix_flake_reference_parse_flags * flags,
    const char * baseDirectory,
    size_t baseDirectoryLen)
{
    nix_clear_err(context);
    try {
        flags->baseDirectory.emplace(nix::Path{std::string(baseDirectory, baseDirectoryLen)});
        return NIX_OK;
    }
    NIXC_CATCH_ERRS
}

nix_err nix_flake_reference_and_fragment_from_string(
    nix_c_context * context,
    nix_fetchers_settings * fetchSettings,
    nix_flake_settings * flakeSettings,
    nix_flake_reference_parse_flags * parseFlags,
    const char * strData,
    size_t strSize,
    nix_flake_reference ** flakeReferenceOut,
    nix_get_string_callback fragmentCallback,
    void * fragmentCallbackUserData)
{
    nix_clear_err(context);
    *flakeReferenceOut = nullptr;
    try {
        std::string str(strData, 0, strSize);

        auto [flakeRef, fragment] =
            nix::parseFlakeRefWithFragment(*fetchSettings->settings, str, parseFlags->baseDirectory, true);
        *flakeReferenceOut = new nix_flake_reference{nix::make_ref<nix::FlakeRef>(flakeRef)};
        return call_nix_get_string_callback(fragment, fragmentCallback, fragmentCallbackUserData);
    }
    NIXC_CATCH_ERRS
}

void nix_flake_reference_free(nix_flake_reference * flakeReference)
{
    delete flakeReference;
}

nix_flake_lock_flags * nix_flake_lock_flags_new(nix_c_context * context, nix_flake_settings * settings)
{
    nix_clear_err(context);
    try {
        auto lockSettings = nix::make_ref<nix::flake::LockFlags>();
        return new nix_flake_lock_flags{lockSettings};
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_flake_lock_flags_free(nix_flake_lock_flags * flags)
{
    delete flags;
}

nix_locked_flake * nix_flake_lock(
    nix_c_context * context,
    nix_flake_settings * settings,
    EvalState * eval_state,
    nix_flake_lock_flags * flags,
    nix_flake_reference * flakeReference)
{
    try {
        auto lockedFlake = nix::make_ref<nix::flake::LockedFlake>(nix::flake::lockFlake(
            *settings->settings, eval_state->state, *flakeReference->flakeRef, *flags->lockFlags));
        return new nix_locked_flake{lockedFlake};
    }
    NIXC_CATCH_ERRS_NULL
}

void nix_locked_flake_free(nix_locked_flake * lockedFlake)
{
    delete lockedFlake;
}

nix_value * nix_locked_flake_get_output_attrs(
    nix_c_context * context, nix_flake_settings * settings, EvalState * evalState, nix_locked_flake * lockedFlake)
{
    nix_clear_err(context);
    try {
        auto v = nix_alloc_value(context, evalState);
        nix::flake::callFlake(evalState->state, *lockedFlake->lockedFlake, v->value);
        return v;
    }
    NIXC_CATCH_ERRS_NULL
}
