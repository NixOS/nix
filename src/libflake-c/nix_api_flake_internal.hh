#pragma once

#include "nix/ref.hh"
#include "nix/flake/settings.hh"

struct nix_flake_settings
{
    nix::ref<nix::flake::Settings> settings;
};
