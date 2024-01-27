#include "auth.hh"
#include "canon-path.hh"
#include "file-system.hh"
#include "users.hh"
#include "util.hh"

namespace nix::auth {

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

    if (!res.protocol)
        throw Error("authentication data '%s' does not contain a protocol", res);

    if (!res.host)
        throw Error("authentication data '%s' does not contain a host", res);

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

    void set(const AuthData & authData) override
    {
    }

    void erase(const AuthData & authData) override
    {
    }
};

Authenticator::Authenticator()
{
    authSources.push_back(make_ref<NixAuthSource>());
}

std::optional<AuthData> Authenticator::fill(const AuthData & request, bool required)
{
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

ref<Authenticator> getAuthenticator()
{
    static auto authenticator = make_ref<Authenticator>();
    return authenticator;
}

}
