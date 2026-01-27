#pragma once
#include <optional>

#include "nix/util/ref.hh"
#include "nix/flake/flake.hh"
#include "nix/flake/flakeref.hh"
#include "nix/flake/settings.hh"

struct nix_flake_settings
{
    nix::ref<nix::flake::Settings> settings;
};

struct nix_flake_reference_parse_flags
{
    std::optional<std::filesystem::path> baseDirectory;
};

struct nix_flake_reference
{
    nix::ref<nix::FlakeRef> flakeRef;
};

struct nix_flake_lock_flags
{
    nix::ref<nix::flake::LockFlags> lockFlags;
};

struct nix_locked_flake
{
    nix::ref<nix::flake::LockedFlake> lockedFlake;
};
