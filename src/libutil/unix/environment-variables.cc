#include <cstdlib>

#include "nix/util/environment-variables.hh"

namespace nix {

int setEnv(const char * name, const char * value)
{
    return ::setenv(name, value, 1);
}

std::optional<std::string> getEnvOs(const std::string & key)
{
    return getEnv(key);
}

int setEnvOs(const OsString & name, const OsString & value)
{
    return setEnv(name.c_str(), value.c_str());
}

} // namespace nix
