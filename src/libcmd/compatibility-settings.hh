#pragma once
#include "config.hh"

namespace nix {
struct CompatibilitySettings : public Config
{

    CompatibilitySettings() = default;

    // Added in Nix 2.24, July 2024.
    Setting<bool> nixShellAlwaysLooksForShellNix{this, true, "nix-shell-always-looks-for-shell-nix", R"(
        Before Nix 2.24, [`nix-shell`](@docroot@/command-ref/nix-shell.md) would only look at `shell.nix` if it was in the working directory - when no file was specified.

        Since Nix 2.24, `nix-shell` always looks for a `shell.nix`, whether that's in the working directory, or in a directory that was passed as an argument.

        You may set this to `false` to revert to the Nix 2.3 behavior.

        This setting is not recommended, and will be deprecated and later removed in the future.
    )"};

    // Added in Nix 2.24, July 2024.
    Setting<bool> nixShellShebangArgumentsRelativeToScript{
        this, true, "nix-shell-shebang-arguments-relative-to-script", R"(
        Before Nix 2.24, the arguments in a `nix-shell` shebang - as well as `--arg` - were relative to working directory.

        Since Nix 2.24, the arguments are relative to the [base directory](@docroot@/glossary.md#gloss-base-directory) defined as the script's directory.

        You may set this to `false` to revert to the Nix 2.3 behavior.

        This setting is not recommended, and will be deprecated and later removed in the future.
    )"};
};

};
