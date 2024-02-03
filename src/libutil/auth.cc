#include "auth.hh"
#include "canon-path.hh"
#include "file-system.hh"
#include "users.hh"
#include "util.hh"
#include "processes.hh"

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
    std::vector<AuthData> authDatas;

    NixAuthSource()
    {
        // FIXME: read the auth directory lazily.
        auto authDir = CanonPath(getDataDir()) + "nix" + "auth";

        if (pathExists(authDir.abs()))
            for (auto & file : readDirectory(authDir.abs())) {
                if (hasSuffix(file.name, "~")) continue;
                auto path = authDir + file.name;
                auto authData = AuthData::parseGitAuthData(readFile(path.abs()));
                if (!authData.password)
                    warn("authentication file '%s' does not contain a password, skipping", path);
                else
                    authDatas.push_back(authData);
            }
    }

    std::optional<AuthData> get(const AuthData & request) override
    {
        for (auto & authData : authDatas)
            if (auto res = authData.match(request))
                return res;

        return std::nullopt;
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

    std::optional<AuthData> get(const AuthData & request) override
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
    { }

    std::optional<AuthData> get(const AuthData & request) override
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
};

Authenticator::Authenticator()
{
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
}

std::optional<AuthData> Authenticator::fill(const AuthData & request, bool required)
{
    if (!request.protocol)
        throw Error("authentication data '%s' does not contain a protocol", request);

    if (!request.host)
        throw Error("authentication data '%s' does not contain a host", request);

    for (auto & authSource : authSources) {
        auto res = authSource->get(request);
        if (res)
            return res;
    }

    return std::nullopt;
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
    static auto authenticator = make_ref<Authenticator>();
    return authenticator;
}

}
