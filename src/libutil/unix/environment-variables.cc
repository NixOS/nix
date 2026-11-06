#include <cstdlib>
#include <cstring>

#include "nix/util/environment-variables.hh"
#include "nix/util/system-error.hh"

extern char ** environ __attribute__((weak));

namespace nix {

std::optional<std::string> getEnvOs(const std::string & key)
{
    char * value = getenv(key.c_str());
    if (!value)
        return {};
    return std::string(value);
}

OsStringMap getEnvOs()
{
    OsStringMap env;
    for (size_t i = 0; environ[i]; ++i) {
        auto s = environ[i];
        auto eq = strchr(s, '=');
        if (!eq)
            // invalid env, just keep going
            continue;
        env.emplace(std::string(s, eq), std::string(eq + 1));
    }
    return env;
}

void setEnvOs(const OsString & name, const OsString & value)
{
    if (::setenv(name.c_str(), value.c_str(), 1) == -1)
        throw SysError("setting environment variable '%s'", name);
}

void unsetEnvOs(const OsChar * name)
{
    if (::unsetenv(name) == -1)
        throw SysError("unsetting environment variable '%s'", name);
}

//////////////////////////////////////////////////////////////////////

/* The portable wrappers. `OsString` is `std::string` on Unix, so there is
   nothing to convert. */

StringMap getEnv()
{
    return getEnvOs();
}

void setEnv(const char * name, const char * value)
{
    setEnvOs(name, value);
}

void unsetEnv(const char * name)
{
    unsetEnvOs(name);
}

} // namespace nix
