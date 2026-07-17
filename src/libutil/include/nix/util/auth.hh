#pragma once
///@file

#include <optional>

#include "nix/util/types.hh"
#include "nix/util/configuration.hh"
#include "nix/util/ref.hh"
#include "nix/util/sync.hh"

namespace nix::auth {

struct AuthSettings : Config
{
    void anchor() override;

    Setting<Strings> authSources{
        this,
        {"builtin:nix"},
        "auth-sources",
        R"(
          A list of credential helper programs from which Nix obtains
          authentication data (user names and passwords) for HTTP
          requests. These helpers use [the same protocol as Git's
          credential helpers](https://git-scm.com/docs/gitcredentials#_custom_helpers),
          so any Git credential helper can be used as an authentication
          source.

          Nix has the following builtin helper:

          * `builtin:nix`: Get authentication data from files in
            `~/.local/share/nix/auth`. Each file uses the
            `git-credential` key-value format. For example, the
            following sets a user name and password for
            `cache.example.org`:

            ```
            # cat <<EOF > ~/.local/share/nix/auth/my-cache
            protocol=https
            host=cache.example.org
            username=alice
            password=foobar
            EOF
            ```

          Any list element that is not prefixed with `builtin:` is
          treated as an external credential helper program.

          Example: `builtin:nix git-credential-libsecret`
        )"};

    Setting<bool> storeAuth{
        this,
        false,
        "store-auth",
        R"(
          Whether to store interactively entered user names and passwords
          using the authentication sources configured in
          [`auth-sources`](#conf-auth-sources).
        )"};
};

extern AuthSettings authSettings;

struct AuthData
{
    std::optional<std::string> protocol;
    std::optional<std::string> host;
    std::optional<std::string> path;
    std::optional<std::string> userName;
    std::optional<std::string> password;

    static AuthData parseGitAuthData(std::string_view raw);

    /**
     * Match this entry against `request`, returning the merged
     * authentication data on success or `std::nullopt` on a mismatch.
     */
    std::optional<AuthData> match(const AuthData & request) const;

    std::string toGitAuthData() const;
};

std::ostream & operator<<(std::ostream & str, const AuthData & authData);

struct AuthSource
{
    virtual ~AuthSource();

    virtual std::optional<AuthData> get(const AuthData & request, bool required) = 0;

    virtual bool set(const AuthData & authData)
    {
        return false;
    }
};

class Authenticator
{
    /**
     * Immutable after construction so it can be read without
     * synchronisation while helpers run.
     */
    const std::vector<ref<AuthSource>> authSources;

    /**
     * Positive cache of resolved credentials. `fill()`/`reject()` may
     * be called concurrently (e.g. from substituter threads).
     */
    Sync<std::vector<AuthData>> cache_;

public:

    Authenticator(std::vector<ref<AuthSource>> authSources = {})
        : authSources(std::move(authSources))
    {
    }

    std::optional<AuthData> fill(const AuthData & request, bool required);

    /**
     * Drop `authData` from the in-process cache so the next `fill()`
     * re-queries the sources. Cheap enough to call from latency-
     * sensitive contexts (e.g. the curl worker thread).
     */
    void reject(const AuthData & authData);
};

ref<Authenticator> getAuthenticator();

} // namespace nix::auth
