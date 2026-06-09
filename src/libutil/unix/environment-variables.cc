#include <cstdlib>
#include <cstring>

#include "nix/util/environment-variables.hh"

extern char ** environ __attribute__((weak));

namespace nix {

int setEnv(const char * name, const char * value)
{
    return ::setenv(name, value, 1);
}

std::optional<std::string> getEnvOs(const std::string & key)
{
    return getEnv(key);
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

StringMap getEnv()
{
    return getEnvOs();
}

int setEnvOs(const OsString & name, const OsString & value)
{
    return setEnv(name.c_str(), value.c_str());
}

int unsetEnvOs(const OsChar * name)
{
    return unsetenv(name);
}

} // namespace nix
