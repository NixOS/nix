#pragma once
///@file

#include "nix/util/types.hh"
#include "nix/util/configuration.hh"
#include "nix/util/ref.hh"
#include "nix/util/sync.hh"

#include <map>
#include <limits>

#include <sys/types.h>

namespace nix {

struct GitRepo;

}

namespace nix::fetchers {

struct Cache;

struct Settings : public Config
{
    Settings();

    Setting<StringMap> accessTokens{
        this,
        {},
        "access-tokens",
        R"(
          Access tokens used to access protected GitHub, GitLab, or
          other locations requiring token-based authentication.

          Access tokens are specified as a string made up of
          space-separated `host=token` values.  The specific token
          used is selected by matching the `host` portion against the
          "host" specification of the input. The `host` portion may
          contain a path element which matches against the prefix
          URL for the input. (eg: `github.com/org=token`). The actual use
          of the `token` value is determined by the type of resource
          being accessed:

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

    Setting<bool> allowDirty{this, true, "allow-dirty", "Whether to allow dirty Git/Mercurial trees."};

    Setting<bool> warnDirty{this, true, "warn-dirty", "Whether to warn about dirty Git/Mercurial trees."};

    Setting<bool> allowDirtyLocks{
        this,
        false,
        "allow-dirty-locks",
        R"(
          Whether to allow dirty inputs (such as dirty Git workdirs)
          to be locked via their NAR hash. This is generally bad
          practice since Nix has no way to obtain such inputs if they
          are subsequently modified. Therefore lock files with dirty
          locks should generally only be used for local testing, and
          should not be pushed to other users.
        )",
        {},
        true,
        Xp::Flakes};

    Setting<bool> trustTarballsFromGitForges{
        this,
        true,
        "trust-tarballs-from-git-forges",
        R"(
          If enabled (the default), Nix considers tarballs from
          GitHub and similar Git forges to be locked if a Git revision
          is specified,
          e.g. `github:NixOS/patchelf/7c2f768bf9601268a4e71c2ebe91e2011918a70f`.
          This requires Nix to trust that the provider returns the
          correct contents for the specified Git revision.

          If disabled, such tarballs are only considered locked if a
          `narHash` attribute is specified,
          e.g. `github:NixOS/patchelf/7c2f768bf9601268a4e71c2ebe91e2011918a70f?narHash=sha256-PPXqKY2hJng4DBVE0I4xshv/vGLUskL7jl53roB8UdU%3D`.
        )"};

    Setting<std::string> flakeRegistry{
        this,
        "https://channels.nixos.org/flake-registry.json",
        "flake-registry",
        R"(
          Path or URI of the global flake registry.

          When empty, disables the global flake registry.
        )",
        {},
        true,
        Xp::Flakes};

    ref<Cache> getCache() const;

    ref<GitRepo> getTarballCache() const;

private:
    mutable Sync<std::shared_ptr<Cache>> _cache;

    mutable Sync<std::shared_ptr<GitRepo>> _tarballCache;
};

} // namespace nix::fetchers
