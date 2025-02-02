#pragma once

#include "ref.hh"
#include "flake/settings.hh"

struct nix_flake_settings
{
    nix::ref<nix::flake::Settings> settings;
};
