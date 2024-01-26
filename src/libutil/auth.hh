#pragma once

#include <optional>

#include "types.hh"

namespace nix::auth {

struct AuthData
{
    std::optional<std::string> protocol;
    std::optional<std::string> host;
    std::optional<std::string> path;
    std::optional<std::string> userName;
    std::optional<std::string> password;

    static AuthData parseGitAuthData(std::string_view raw);

    std::optional<AuthData> match(const AuthData & request);
};

std::ostream & operator << (std::ostream & str, const AuthData & authData);

struct AuthSource
{
    virtual std::optional<AuthData> get(const AuthData & request) = 0;

    virtual void set(const AuthData & authData) = 0;

    virtual void erase(const AuthData & authData) = 0;
};

class Authenticator
{
    std::vector<ref<AuthSource>> authSources;

public:

    Authenticator();

    std::optional<AuthData> fill(const AuthData & request, bool required);

    void addAuthSource(ref<AuthSource> authSource);
};

ref<Authenticator> getAuthenticator();

}
