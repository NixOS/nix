#pragma once

#include <optional>

#include "types.hh"
#include "config.hh"

namespace nix::auth {

enum struct AuthForwarding { Disabled, TrustedUsers, AllUsers };

struct AuthSettings : Config
{
    Setting<Strings> authSources{
        this,
        {"builtin:nix", "builtin:netrc"},
        "auth-sources",
        R"(
          A list of helper programs from which to obtain
          authentication data for HTTP requests.  These helpers use
          [the same protocol as Git's credential
          helpers](https://git-scm.com/docs/gitcredentials#_custom_helpers),
          so any Git credential helper can be used as an
          authentication source.

          Nix has the following builtin helpers:

          * `builtin:nix`: Get authentication data from files in
            `~/.local/share/nix/auth`. For example, the following sets
            a username and password for `cache.example.org`:

            ```
            # cat <<EOF > ~/.local/share/nix/auth/my-cache
            protocol=https
            host=cache.example.org
            username=alice
            password=foobar
            EOF
            ```

          Example: `builtin:nix` `git-credential-libsecret`
        )"};

    Setting<Path> netrcFile{
        this, "", "netrc-file",
        R"(
          An absolute path to a `netrc` file. Nix will use the HTTP
          authentication credentials in this file when trying to download from
          a remote host through HTTP or HTTPS. Defaults to
          `$NIX_CONF_DIR/netrc`.

          The `netrc` file consists of a list of accounts in the following
          format:

              machine my-machine
              login my-username
              password my-password

          For the exact syntax, see [the `curl`
          documentation](https://ec.haxx.se/usingcurl-netrc.html).

          > **Note**
          >
          > This must be an absolute path, and `~` is not resolved. For
          > example, `~/.netrc` won't resolve to your home directory's
          > `.netrc`.
        )"};

    Setting<bool> storeAuth{
        this, false, "store-auth",
        R"(
          Whether to store user names and passwords using the
          authentication sources configured in [`auth-sources`](#conf-auth-sources).
        )"};

    Setting<AuthForwarding> authForwarding{
        this, AuthForwarding::TrustedUsers, "auth-forwarding",
        R"(
          Whether to forward authentication data to the Nix daemon. This setting can have the following values:

          * `false`: Forwarding is disabled.
          * `trusted-users`: Forwarding is only supported for [trusted users](#conf-trusted-users).
          * `all-users`: Forwarding is supported for all users.
        )",
        {}, true, Xp::AuthForwarding};
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

    std::optional<AuthData> match(const AuthData & request) const;

    std::string toGitAuthData() const;
};

std::ostream & operator << (std::ostream & str, const AuthData & authData);

struct AuthSource
{
    virtual ~AuthSource()
    { }

    virtual std::optional<AuthData> get(const AuthData & request, bool required) = 0;

    virtual bool set(const AuthData & authData)
    { return false; }

    virtual void erase(const AuthData & authData)
    { }
};

class Authenticator
{
    std::vector<ref<AuthSource>> authSources;

    std::vector<AuthData> cache;

public:

    Authenticator(std::vector<ref<AuthSource>> authSources = {})
        : authSources(std::move(authSources))
    { }

    std::optional<AuthData> fill(const AuthData & request, bool required);

    void reject(const AuthData & authData);

    void addAuthSource(ref<AuthSource> authSource);

    void setAuthSource(ref<AuthSource> authSource);
};

ref<Authenticator> getAuthenticator();

}
