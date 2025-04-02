#pragma once

#include "nix/util/ref.hh"
#include "nix/flake/settings.hh"

struct nix_flake_settings
{
    nix::ref<nix::flake::Settings> settings;
};
