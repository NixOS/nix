#pragma once
#include "nix/util/configuration.hh"

namespace nix {
struct CompatibilitySettings : public Config
{

    CompatibilitySettings() = default;

    // Added in Nix 2.24, July 2024.
    Setting<bool> nixShellAlwaysLooksForShellNix{this, true, "nix-shell-always-looks-for-shell-nix", R"(
        Before Nix 2.24, [`nix-shell`](@docroot@/command-ref/nix-shell.md) would only look at `shell.nix` if it was in the working directory - when no file was specified.

        Since Nix 2.24, `nix-shell` always looks for a `shell.nix`, whether that's in the working directory, or in a directory that was passed as an argument.

        You may set this to `false` to temporarily revert to the behavior of Nix 2.23 and older.

        Using this setting is not recommended.
        It will be deprecated and removed.
    )"};

    // Added in Nix 2.24, July 2024.
    Setting<bool> nixShellShebangArgumentsRelativeToScript{
        this, true, "nix-shell-shebang-arguments-relative-to-script", R"(
        Before Nix 2.24, relative file path expressions in arguments in a `nix-shell` shebang were resolved relative to the working directory.

        Since Nix 2.24, `nix-shell` resolves these paths in a manner that is relative to the [base directory](@docroot@/glossary.md#gloss-base-directory), defined as the script's directory.

        You may set this to `false` to temporarily revert to the behavior of Nix 2.23 and older.

        Using this setting is not recommended.
        It will be deprecated and removed.
    )"};
};

}; // namespace nix
