#include "nix_api_flake.h"
#include "nix_api_flake_internal.hh"
#include "nix_api_util_internal.h"
#include "nix_api_expr_internal.h"

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
