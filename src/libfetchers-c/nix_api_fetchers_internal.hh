#pragma once
#include "nix/fetchers/fetch-settings.hh"
#include "nix/util/ref.hh"

/**
 * A shared reference to `nix::fetchers::Settings`
 * @see nix::fetchers::Settings
 */
struct nix_fetchers_settings
{
    nix::ref<nix::fetchers::Settings> settings;
};
