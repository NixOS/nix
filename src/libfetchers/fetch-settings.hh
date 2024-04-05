#pragma once
///@file

#include "types.hh"
#include "config.hh"

#include <map>
#include <limits>

#include <sys/types.h>

namespace nix {

struct FetchSettings : public Config
{
    FetchSettings();

    Setting<StringMap> accessTokens{this, {}, "access-tokens",
        R"(
          Access tokens used to access protected GitHub, GitLab, or
          other locations requiring token-based authentication.

          Access tokens are specified as a string made up of
          space-separated `host=token` values.  The specific token
          used is selected by matching the `host` portion against the
          "host" specification of the input. The actual use of the
          `token` value is determined by the type of resource being
          accessed:

          * Github: the token value is the OAUTH-TOKEN string obtained
            as the Personal Access Token from the Github server (see
            https://docs.github.com/en/developers/apps/building-oauth-apps/authorizing-oauth-apps).

          * Gitlab: the token value is either the OAuth2 token or the
            Personal Access Token (these are different types tokens
            for gitlab, see
            https://docs.gitlab.com/12.10/ee/api/README.html#authentication).
            The `token` value should be `type:tokenstring` where
            `type` is either `OAuth2` or `PAT` to indicate which type
            of token is being specified.

          Example `~/.config/nix/nix.conf`:

          ```
          access-tokens = github.com=23ac...b289 gitlab.mycompany.com=PAT:A123Bp_Cd..EfG gitlab.com=OAuth2:1jklw3jk
          ```

          Example `~/code/flake.nix`:

          ```nix
          input.foo = {
            type = "gitlab";
            host = "gitlab.mycompany.com";
            owner = "mycompany";
            repo = "pro";
          };
          ```

          This example specifies three tokens, one each for accessing
          github.com, gitlab.mycompany.com, and gitlab.com.

          The `input.foo` uses the "gitlab" fetcher, which might
          requires specifying the token type along with the token
          value.
          )"};

    Setting<bool> allowDirty{this, true, "allow-dirty",
        "Whether to allow dirty Git/Mercurial trees."};

    Setting<bool> warnDirty{this, true, "warn-dirty",
        "Whether to warn about dirty Git/Mercurial trees."};

    Setting<std::string> flakeRegistry{this, "https://channels.nixos.org/flake-registry.json", "flake-registry",
        R"(
          Path or URI of the global flake registry.

          When empty, disables the global flake registry.
        )",
        {}, true, Xp::Flakes};

    Setting<bool> useRegistries{this, true, "use-registries",
        "Whether to use flake registries to resolve flake references.",
        {}, true, Xp::Flakes};

    Setting<bool> acceptFlakeConfig{this, false, "accept-flake-config",
        "Whether to accept nix configuration from a flake without prompting.",
        {}, true, Xp::Flakes};

    Setting<std::string> commitLockFileSummary{
        this, "", "commit-lockfile-summary",
        R"(
          The commit summary to use when committing changed flake lock files. If
          empty, the summary is generated based on the action performed.
        )",
        {}, true, Xp::Flakes};

    Setting<bool> trustTarballsFromGitForges{
        this, true, "trust-tarballs-from-git-forges",
        R"(
          If enabled (the default), Nix will consider tarballs from
          GitHub and similar Git forges to be locked if a Git revision
          is specified,
          e.g. `github:NixOS/patchelf/7c2f768bf9601268a4e71c2ebe91e2011918a70f`.
          This requires Nix to trust that the provider will return the
          correct contents for the specified Git revision.

          If disabled, such tarballs are only considered locked if a
          `narHash` attribute is specified,
          e.g. `github:NixOS/patchelf/7c2f768bf9601268a4e71c2ebe91e2011918a70f?narHash=sha256-PPXqKY2hJng4DBVE0I4xshv/vGLUskL7jl53roB8UdU%3D`.
        )"};

};

// FIXME: don't use a global variable.
extern FetchSettings fetchSettings;

}
