#pragma once
///@file

#include <mutex>
#include <optional>

#include "nix/util/types.hh"
#include "nix/util/configuration.hh"
#include "nix/util/ref.hh"

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

    virtual void erase(const AuthData & authData) {}
};

class Authenticator
{
    /**
     * Guards `authSources` and `cache`. `fill()`/`reject()` may be
     * called concurrently (e.g. from substituter threads).
     */
    std::mutex mutex;

    std::vector<ref<AuthSource>> authSources;

    std::vector<AuthData> cache;

public:

    Authenticator(std::vector<ref<AuthSource>> authSources = {})
        : authSources(std::move(authSources))
    {
    }

    std::optional<AuthData> fill(const AuthData & request, bool required);

    void reject(const AuthData & authData);

    void addAuthSource(ref<AuthSource> authSource);

    void setAuthSource(ref<AuthSource> authSource);
};

ref<Authenticator> getAuthenticator();

} // namespace nix::auth
