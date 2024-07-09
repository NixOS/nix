#pragma once
#include "config.hh"

namespace nix {
struct CompatibilitySettings : public Config
{

    CompatibilitySettings() = default;

    Setting<bool> nixShellAlwaysLooksForShellNix{this, true, "nix-shell-always-looks-for-shell-nix", R"(
        Before Nix 2.24, [`nix-shell`](@docroot@/command-ref/nix-shell.md) would only look at `shell.nix` if it was in the working directory - when no file was specified.

        Since Nix 2.24, `nix-shell` always looks for a `shell.nix`, whether that's in the working directory, or in a directory that was passed as an argument.

        You may set this to `false` to revert to the Nix 2.3 behavior.
    )"};
};

};
