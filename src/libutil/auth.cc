#include "nix/util/auth.hh"
#include "nix/util/file-system.hh"
#include "nix/util/users.hh"
#include "nix/util/util.hh"
#include "nix/util/processes.hh"
#include "nix/util/os-string.hh"
#include "nix/util/environment-variables.hh"
#include "nix/util/config-global.hh"

#include <filesystem>

namespace nix::auth {

AuthSettings authSettings;

void AuthSettings::anchor() {}

static GlobalConfig::Register rAuthSettings(&authSettings);

AuthSource::~AuthSource() = default;

AuthData AuthData::parseGitAuthData(std::string_view raw)
{
    AuthData res;

    for (auto & line : tokenizeString<std::vector<std::string>>(raw, "\n")) {
        auto eq = line.find('=');
        if (eq == line.npos)
            continue;
        auto key = trim(line.substr(0, eq));
        /* Don't trim the value; passwords may contain leading/trailing spaces. */
        auto value = line.substr(eq + 1);
        if (!value.empty() && value.back() == '\r')
            value.pop_back();
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

    /* `request.path` must be within `path`. */
    if (path && request.path && !(*path == *request.path || request.path->substr(0, path->size() + 1) == *path + "/"))
        return std::nullopt;

    if (userName && request.userName && *userName != *request.userName)
        return std::nullopt;

    if (password && request.password && *password != *request.password)
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
    if (protocol)
        res += fmt("protocol=%s\n", *protocol);
    if (host)
        res += fmt("host=%s\n", *host);
    if (path)
        res += fmt("path=%s\n", *path);
    if (userName)
        res += fmt("username=%s\n", *userName);
    if (password)
        res += fmt("password=%s\n", *password);
    return res;
}

std::ostream & operator<<(std::ostream & str, const AuthData & authData)
{
    str << fmt(
        "{protocol = %s, host = %s, path = %s, userName = %s, password = %s}",
        authData.protocol.value_or(""),
        authData.host.value_or(""),
        authData.path.value_or(""),
        authData.userName.value_or(""),
        authData.password ? "..." : "");
    return str;
}

namespace {

struct NixAuthSource : AuthSource
{
    const std::filesystem::path authDir;

    std::vector<AuthData> authDatas;

    NixAuthSource()
        : authDir(getDataDir() / "auth")
    {
        if (pathExists(authDir))
            for (auto & file : std::filesystem::directory_iterator{authDir}) {
                if (!file.is_regular_file() || hasSuffix(file.path().filename().string(), "~"))
                    continue;
                auto authData = AuthData::parseGitAuthData(readFile(file.path().string()));
                /* Require a host; a hostless entry would match (and leak its
                   password to) every request. */
                if (!authData.host || !authData.password)
                    warn(
                        "ignoring authentication file '%s': it must specify a host and a password",
                        PathFmt(file.path()));
                else
                    authDatas.push_back(std::move(authData));
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
        if (get(authData, false))
            return true;

        auto authFile = authDir / fmt("auto-%s-%s", authData.host.value_or("none"), authData.userName.value_or("none"));

        createDirs(authDir);
        writeFile(authFile.string(), authData.toGitAuthData(), 0600);
        authDatas.push_back(authData);

        return true;
    }
};

/**
 * Authenticate using an external helper program via the
 * `git-credential-*` protocol.
 */
struct ExternalAuthSource : AuthSource
{
    bool enabled = true;
    std::filesystem::path program;

    ExternalAuthSource(std::filesystem::path program)
        : program(std::move(program))
    {
    }

    std::optional<std::string> run(const std::string & verb, const AuthData & authData)
    {
        if (!enabled)
            return std::nullopt;
        try {
            return runProgram(
                       RunOptions{
                           .program = program,
                           .lookupPath = true,
                           .args = {string_to_os_string(verb)},
                           .input = authData.toGitAuthData(),
                       })
                .second;
        } catch (SysError & e) {
            ignoreExceptionExceptInterrupt();
            if (e.errNo == ENOENT || e.errNo == EPIPE)
                enabled = false;
            return std::nullopt;
        } catch (Error &) {
            ignoreExceptionExceptInterrupt();
            return std::nullopt;
        }
    }

    std::optional<AuthData> get(const AuthData & request, bool required) override
    {
        auto output = run("get", request);
        if (!output)
            return std::nullopt;

        auto response = AuthData::parseGitAuthData(*output);
        if (!response.password)
            return std::nullopt;

        AuthData res{request};
        if (response.userName)
            res.userName = response.userName;
        res.password = response.password;
        return res;
    }

    bool set(const AuthData & authData) override
    {
        return run("store", authData).has_value();
    }

    void erase(const AuthData & authData) override
    {
        run("erase", authData);
    }
};

} // namespace

std::optional<AuthData> Authenticator::fill(const AuthData & request, bool required)
{
    if (!request.protocol)
        throw Error("authentication request %s does not specify a protocol", request);

    if (!request.host)
        throw Error("authentication request %s does not specify a host", request);

    std::lock_guard<std::mutex> lock(mutex);

    for (auto & entry : cache)
        if (auto res = entry.match(request)) {
            debug("authentication cache hit for %s", request);
            return res;
        }

    for (auto & authSource : authSources)
        if (auto res = authSource->get(request, required)) {
            cache.push_back(*res);
            return res;
        }

    if (required) {
        auto askPassHelper = getEnvNonEmpty("SSH_ASKPASS");
        if (!askPassHelper)
            return std::nullopt;

        /* See https://github.com/KDE/ksshaskpass/blob/master/src/main.cpp
           for the expected format of the phrases. */
        auto res = request;

        if (!res.userName)
            res.userName = chomp(runProgram(
                *askPassHelper, true, {string_to_os_string(fmt("Username for '%s': ", *request.host))}, true));

        if (!res.password)
            res.password = chomp(runProgram(
                *askPassHelper, true, {string_to_os_string(fmt("Password for '%s': ", *request.host))}, true));

        if (res.userName && res.password) {
            cache.push_back(res);
            if (authSettings.storeAuth)
                for (auto & authSource : authSources)
                    if (authSource->set(res))
                        break;
            return res;
        }
    }

    return std::nullopt;
}

void Authenticator::reject(const AuthData & authData)
{
    debug("rejecting authentication data %s", authData);
    std::lock_guard<std::mutex> lock(mutex);
    std::erase_if(cache, [&](const AuthData & entry) { return entry.match(authData).has_value(); });
    for (auto & authSource : authSources)
        authSource->erase(authData);
}

void Authenticator::addAuthSource(ref<AuthSource> authSource)
{
    std::lock_guard<std::mutex> lock(mutex);
    authSources.push_back(authSource);
}

void Authenticator::setAuthSource(ref<AuthSource> authSource)
{
    std::lock_guard<std::mutex> lock(mutex);
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
                else
                    warn("unknown authentication source '%s'", s);
            } else
                authSources.push_back(make_ref<ExternalAuthSource>(s));
        }

        make_ref<Authenticator>(std::move(authSources));
    });
    return authenticator;
}

} // namespace nix::auth
