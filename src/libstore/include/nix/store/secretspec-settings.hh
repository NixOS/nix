#pragma once
///@file

#include "nix/store/config.hh"
#include "nix/util/configuration.hh"
#include "nix/util/sync.hh"

#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace nix {

struct SecretSpecCache;

/** The settings that together select one SecretSpec resolution. */
struct SecretSpecRequest
{
    std::string file, provider, profile, scope;

    auto operator<=>(const SecretSpecRequest &) const = default;
};

/** Shared SecretSpec resolution context for Nix credential consumers. */
struct SecretSpecSettings : public virtual Config
{
private:
    static std::string defaultFile();

public:
    SecretSpecSettings();
    ~SecretSpecSettings();

    Setting<std::string> file{
        this,
        defaultFile(),
        "secretspec-file",
        R"(
          Path to the `secretspec.toml` used by SecretSpec-backed Nix
          credential settings.

          The default is a manifest bundled with Nix. It declares the optional
          `GITHUB_TOKEN`, `GITLAB_TOKEN`, `SOURCEHUT_TOKEN`, `NIX_NETRC`, and
          `BUILD_TOKEN` secrets used by the examples for the SecretSpec-backed
          settings. Set this option to a custom manifest to use other secret
          names or declarations. Set it to an empty value to make SecretSpec
          search for a manifest from the current working directory in the same
          way as its CLI and SDKs.

          Credentials that a build needs, such as
          [`secretspec-impure-env`](#conf-secretspec-impure-env), are resolved
          by whichever process runs the build. When that is the Nix daemon, all
          `secretspec-*` settings are forwarded to it and resolution happens in
          the daemon's context. If this setting is empty, manifest discovery
          starts from the daemon's working directory rather than yours, and
          providers that require a user session, such as the OS keyring, are not
          reachable. When overriding the bundled manifest for a daemon, set an
          absolute path here and choose a provider the daemon can read.
        )",
        {},
        true,
        std::nullopt,
        FlakeConfigSetting::Forbidden};

    Setting<std::string> provider{
        this,
        "",
        "secretspec-provider",
        R"(
          Optional SecretSpec provider name or URI used by SecretSpec-backed
          Nix credential settings.

          If unset, SecretSpec uses `SECRETSPEC_PROVIDER`, its global
          configuration, and the manifest's provider configuration in their
          normal precedence order.
        )",
        {},
        true,
        std::nullopt,
        FlakeConfigSetting::Forbidden};

    Setting<std::string> profile{
        this,
        "",
        "secretspec-profile",
        R"(
          Optional SecretSpec profile used by SecretSpec-backed Nix credential
          settings.

          If unset, SecretSpec uses `SECRETSPEC_PROFILE`, its global default,
          or the `default` profile.
        )",
        {},
        true,
        std::nullopt,
        FlakeConfigSetting::Forbidden};

    Setting<std::string> scope{
        this,
        "",
        "secretspec-scope",
        R"(
          Optional SecretSpec scope used by SecretSpec-backed Nix credential
          settings.

          A dedicated scope containing only credentials used by Nix limits the
          secrets returned across the FFI boundary and prevents unrelated
          required secrets from blocking credential resolution.
        )",
        {},
        true,
        std::nullopt,
        FlakeConfigSetting::Forbidden};

    /** Resolve an inline secret, rejecting secrets declared with `as_path`. */
    std::string getInlineSecret(const std::string & name) const;

    /** Resolve an `as_path` secret and retain ownership of its temporary file. */
    AbsolutePath getPathSecret(const std::string & name) const;

private:
    std::shared_ptr<SecretSpecCache> getResolvedSecrets() const;

    /* Keep every resolved context alive so an in-flight user never observes an
       `as_path` file being deleted after a configuration change. */
    mutable Sync<std::map<SecretSpecRequest, std::shared_ptr<SecretSpecCache>>> _caches;

    /* Serializes resolution so that a request is resolved only once, without
       blocking cache lookups for the duration of the resolution. */
    mutable std::mutex _resolveMutex;
};

extern SecretSpecSettings secretSpecSettings;

} // namespace nix
