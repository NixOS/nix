#pragma once

#include "types.hh"
#include "config.hh"
#include "util.hh"

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
          github.com, gitlab.mycompany.com, and sourceforge.net.

          The `input.foo` uses the "gitlab" fetcher, which might
          requires specifying the token type along with the token
          value.
          )"};

    Setting<bool> allowDirty{this, true, "allow-dirty",
        "Whether to allow dirty Git/Mercurial trees."};

    Setting<bool> warnDirty{this, true, "warn-dirty",
        "Whether to warn about dirty Git/Mercurial trees."};

    Setting<std::string> flakeRegistry{this, "https://github.com/NixOS/flake-registry/raw/master/flake-registry.json", "flake-registry",
        "Path or URI of the global flake registry."};

    Setting<bool> useRegistries{this, true, "use-registries",
        "Whether to use flake registries to resolve flake references."};

    Setting<bool> acceptFlakeConfig{this, false, "accept-flake-config",
        "Whether to accept nix configuration from a flake without prompting."};

    Setting<std::string> commitLockFileSummary{
        this, "", "commit-lockfile-summary",
        R"(
          The commit summary to use when committing changed flake lock files. If
          empty, the summary is generated based on the action performed.
        )"};
};

// FIXME: don't use a global variable.
extern FetchSettings fetchSettings;

}
