#include "nix_api_flake.h"
#include "nix_api_flake_internal.hh"
#include "nix_api_util_internal.h"

#include "flake/flake.hh"

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

nix_err nix_flake_init_global(nix_c_context * context, nix_flake_settings * settings)
{
    static std::shared_ptr<nix::flake::Settings> registeredSettings;
    try {
        if (registeredSettings)
            throw nix::Error("nix_flake_init_global already initialized");

        registeredSettings = settings->settings;
        nix::flake::initLib(*registeredSettings);
    }
    NIXC_CATCH_ERRS
}
