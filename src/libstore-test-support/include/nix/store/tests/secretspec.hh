#pragma once
///@file

#include <filesystem>
#include <string_view>

#include "nix/store/secretspec-settings.hh"
#include "nix/util/file-system.hh"

namespace nix::testing {

/**
 * A throwaway `secretspec.toml` plus a `dotenv://` provider backing it, for
 * tests that need real secret resolution without a real secret store.
 */
struct SecretSpecFixture
{
    std::filesystem::path dir = createTempDir();
    AutoDelete cleanup{dir};
    std::filesystem::path manifest = dir / "secretspec.toml";
    std::filesystem::path dotenv = dir / ".env";

    SecretSpecFixture(std::string_view manifestContents, std::string_view dotenvContents)
    {
        writeFile(manifest, manifestContents);
        writeFile(dotenv, dotenvContents);
    }

    void configure(SecretSpecSettings & settings, std::optional<std::string> scope = std::nullopt) const
    {
        settings.file = manifest.string();
        settings.provider = "dotenv://" + dotenv.string();
        settings.profile = "nix";
        if (scope)
            settings.scope = *scope;
    }
};

} // namespace nix::testing
