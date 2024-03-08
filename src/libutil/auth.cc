#include "auth.hh"
#include "file-system.hh"
#include "users.hh"
#include "util.hh"
#include "processes.hh"
#include "environment-variables.hh"
#include "config-impl.hh"
#include "abstract-setting-to-json.hh"

namespace nix {

using namespace auth;

// FIXME: need to generalize defining enum settings.
template<> AuthForwarding BaseSetting<AuthForwarding>::parse(const std::string & str) const
{
    if (str == "false") return AuthForwarding::Disabled;
    else if (str == "trusted-users") return AuthForwarding::TrustedUsers;
    else if (str == "all-users") return AuthForwarding::AllUsers;
    else throw UsageError("option '%s' has invalid value '%s'", name, str);
}

template<> struct BaseSetting<AuthForwarding>::trait
{
    static constexpr bool appendable = false;
};

template<> std::string BaseSetting<AuthForwarding>::to_string() const
{
    if (value == AuthForwarding::Disabled) return "false";
    else if (value == AuthForwarding::TrustedUsers) return "trusted-users";
    else if (value == AuthForwarding::AllUsers) return "all-users";
    else abort();
}

NLOHMANN_JSON_SERIALIZE_ENUM(AuthForwarding, {
    {AuthForwarding::Disabled, "false"},
    {AuthForwarding::TrustedUsers, "trusted-users"},
    {AuthForwarding::AllUsers, "all-users"},
});

}

namespace nix::auth {

AuthSettings authSettings;

static GlobalConfig::Register rAuthSettings(&authSettings);

AuthData AuthData::parseGitAuthData(std::string_view raw)
{
    AuthData res;

    for (auto & line : tokenizeString<std::vector<std::string>>(raw, "\n")) {
        auto eq = line.find('=');
        if (eq == line.npos) continue;
        auto key = trim(line.substr(0, eq));
        auto value = trim(line.substr(eq + 1));
        if (key == "protocol")
            res.protocol = value;
        else if (key == "host")
            res.host = value;
        else if (key == "path")
            res.path = value;
        else if (key == "username")
            res.userName = value;
        else if (key == "password")
            res.password = value;
    }

    return res;
}

std::optional<AuthData> AuthData::match(const AuthData & request) const
{
    if (protocol && request.protocol && *protocol != *request.protocol)
        return std::nullopt;

    if (host && request.host && *host != *request.host)
        return std::nullopt;

    // `request.path` must be within `path`.
    if (path && request.path && !(*path == *request.path || request.path->substr(0, path->size() + 1) == *request.path + "/"))
        return std::nullopt;

    if (userName && request.userName && *userName != request.userName)
        return std::nullopt;

    if (password && request.password && *password != request.password)
        return std::nullopt;

    AuthData res{request};
    if (!res.userName)
        res.userName = userName;
    if (!res.password)
        res.password = password;
    return res;
}

std::string AuthData::toGitAuthData() const
{
    std::string res;
    if (protocol) res += fmt("protocol=%s\n", *protocol);
    if (host) res += fmt("host=%s\n", *host);
    if (path) res += fmt("path=%s\n", *path);
    if (userName) res += fmt("username=%s\n", *userName);
    if (password) res += fmt("password=%s\n", *password);
    return res;
}

std::ostream & operator << (std::ostream & str, const AuthData & authData)
{
    str << fmt("{protocol = %s, host=%s, path=%s, userName=%s, password=%s}",
        authData.protocol.value_or(""),
        authData.host.value_or(""),
        authData.path.value_or(""),
        authData.userName.value_or(""),
        authData.password ? "..." : "");
    return str;
}

struct NixAuthSource : AuthSource
{
    const std::filesystem::path authDir;

    std::vector<AuthData> authDatas;

    NixAuthSource()
        : authDir(std::filesystem::path(getDataDir()) / "nix" / "auth")
    {
        if (pathExists(authDir))
            for (auto & file : readDirectory(authDir)) {
                if (hasSuffix(file.name, "~")) continue;
                auto path = authDir / file.name;
                auto authData = AuthData::parseGitAuthData(readFile(path));
                if (!authData.password)
                    warn("authentication file '%s' does not contain a password, skipping", path);
                else
                    authDatas.push_back(authData);
            }
    }

    std::optional<AuthData> get(const AuthData & request, bool required) override
    {
        for (auto & authData : authDatas)
            if (auto res = authData.match(request))
                return res;

        return std::nullopt;
    }

    bool set(const AuthData & authData) override
    {
        if (get(authData, false)) return true;

        auto authFile = authDir / fmt("auto-%s-%s", authData.host.value_or("none"), authData.userName.value_or("none"));

        writeFile(authFile, authData.toGitAuthData());

        return true;
    }
};

struct NetrcAuthSource : AuthSource
{
    const Path path;
    std::vector<AuthData> authDatas;

    NetrcAuthSource(const Path & path)
        : path(path)
    {
        // FIXME: read netrc lazily.
        debug("reading netrc '%s'", path);

        if (!pathExists(path)) return;

        auto raw = readFile(path);

        std::string_view remaining(raw);

        auto whitespace = "\n\r\t ";

        auto nextToken = [&]() -> std::optional<std::string_view>
        {
            // Skip whitespace.
            auto n = remaining.find_first_not_of(whitespace);
            if (n == remaining.npos) return std::nullopt;
            remaining = remaining.substr(n);

            if (remaining.substr(0, 1) == "\"")
                throw UnimplementedError("quoted tokens in netrc are not supported yet");

            n = remaining.find_first_of(whitespace);
            auto token = remaining.substr(0, n);
            remaining = remaining.substr(n == remaining.npos ? remaining.size() : n);

            return token;
        };

        std::optional<AuthData> curMachine;

        auto flushMachine = [&]()
        {
            if (curMachine) {
                authDatas.push_back(std::move(*curMachine));
                curMachine.reset();
            }
        };

        while (auto token = nextToken()) {
            if (token == "machine") {
                flushMachine();
                auto name = nextToken();
                if (!name) throw Error("netrc 'machine' token requires a name");
                curMachine = AuthData {
                    .protocol = "https",
                    .host = std::string(*name)
                };
            }
            else if (token == "default") {
                flushMachine();
                curMachine = AuthData {
                    .protocol = "https",
                };
            }
            else if (token == "login") {
                if (!curMachine) throw Error("netrc 'login' token must be preceded by a 'machine'");
                auto userName = nextToken();
                if (!userName) throw Error("netrc 'login' token requires a user name");
                curMachine->userName = std::string(*userName);
            }
            else if (token == "password") {
                if (!curMachine) throw Error("netrc 'password' token must be preceded by a 'machine'");
                auto password = nextToken();
                if (!password) throw Error("netrc 'password' token requires a password");
                curMachine->password = std::string(*password);
            }
            else if (token == "account") {
                // Ignore this.
                nextToken();
            }
            else
                warn("unrecognized netrc token '%s'", *token);
        }

        flushMachine();
    }

    std::optional<AuthData> get(const AuthData & request, bool required) override
    {
        for (auto & authData : authDatas)
            if (auto res = authData.match(request))
                return res;

        return std::nullopt;
    }
};

/**
 * Authenticate using an external helper program via the
 * `git-credential-*` protocol.
 */
struct ExternalAuthSource : AuthSource
{
    bool enabled = true;
    Path program;

    ExternalAuthSource(Path program)
        : program(program)
    {
        experimentalFeatureSettings.require(Xp::PluggableAuth);
    }

    std::optional<AuthData> get(const AuthData & request, bool required) override
    {
        try {
            if (!enabled) return std::nullopt;

            auto response = AuthData::parseGitAuthData(
                runProgram(program, true, {"get"}, request.toGitAuthData()));

            if (!response.password)
                return std::nullopt;

            AuthData res{request};
            if (response.userName) res.userName = response.userName;
            res.password = response.password;
            return res;
        } catch (SysError & e) {
            ignoreException();
            if (e.errNo == ENOENT || e.errNo == EPIPE)
                enabled = false;
            return std::nullopt;
        } catch (Error &) {
            ignoreException();
            return std::nullopt;
        }
    }

    bool set(const AuthData & authData) override
    {
        try {
            if (!enabled) return false;

            runProgram(program, true, {"store"}, authData.toGitAuthData());

            return true;
        } catch (SysError & e) {
            ignoreException();
            if (e.errNo == ENOENT || e.errNo == EPIPE)
                enabled = false;
            return false;
        } catch (Error &) {
            ignoreException();
            return false;
        }
    }

    void erase(const AuthData & authData) override
    {
        try {
            if (!enabled) return;

            runProgram(program, true, {"erase"}, authData.toGitAuthData());
        } catch (SysError & e) {
            ignoreException();
            if (e.errNo == ENOENT || e.errNo == EPIPE)
                enabled = false;
        } catch (Error &) {
            ignoreException();
        }
    }
};

std::optional<AuthData> Authenticator::fill(const AuthData & request, bool required)
{
    if (!request.protocol)
        throw Error("authentication data '%s' does not contain a protocol", request);

    if (!request.host)
        throw Error("authentication data '%s' does not contain a host", request);

    for (auto & entry : cache) {
        if (auto res = entry.match(request)) {
            debug("authentication cache hit %s -> %s", entry, *res);
            return res;
        }
    }

    for (auto & authSource : authSources) {
        auto res = authSource->get(request, required);
        if (res) {
            cache.push_back(*res);
            return res;
        }
    }

    if (required) {
        auto askPassHelper = getEnvNonEmpty("SSH_ASKPASS");
        if (askPassHelper) {
            /* Ask the user. */
            auto res = request;

            // Note: see
            // https://github.com/KDE/ksshaskpass/blob/master/src/main.cpp
            // for the expected format of the phrases.

            if (!request.userName) {
                res.userName = chomp(
                    runProgram(*askPassHelper, true,
                        {fmt("Username for '%s': ", request.host.value_or(""))},
                        std::nullopt, true));
            }

            if (!request.password) {
                res.password = chomp(
                    runProgram(*askPassHelper, true,
                        {fmt("Password for '%s': ", request.host.value_or(""))},
                        std::nullopt, true));
            }

            if (res.userName && res.password) {
                cache.push_back(res);

                if (authSettings.storeAuth) {
                    for (auto & authSource : authSources) {
                        if (authSource->set(res))
                            break;
                    }
                }
            }

            return res;
        }
    }

    return std::nullopt;
}

void Authenticator::reject(const AuthData & authData)
{
    debug("erasing auth data %s", authData);
    for (auto & authSource : authSources)
        authSource->erase(authData);
}

void Authenticator::addAuthSource(ref<AuthSource> authSource)
{
    authSources.push_back(authSource);
}

void Authenticator::setAuthSource(ref<AuthSource> authSource)
{
    authSources = {authSource};
}

ref<Authenticator> getAuthenticator()
{
    static auto authenticator = ({
        std::vector<ref<AuthSource>> authSources;

        for (auto & s : authSettings.authSources.get()) {
            if (hasPrefix(s, "builtin:")) {
                if (s == "builtin:nix")
                    authSources.push_back(make_ref<NixAuthSource>());
                else if (s == "builtin:netrc") {
                    if (authSettings.netrcFile != "")
                        authSources.push_back(make_ref<NetrcAuthSource>(authSettings.netrcFile));
                }
                else
                    warn("unknown authentication sources '%s'", s);
            } else
                authSources.push_back(make_ref<ExternalAuthSource>(s));
        }

        make_ref<Authenticator>(authSources);
    });
    return authenticator;
}

}
