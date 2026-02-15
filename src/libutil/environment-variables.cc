#include "nix/util/util.hh"
#include "nix/util/environment-variables.hh"

namespace nix {

std::optional<std::string> getEnv(const std::string & key)
{
    char * value = getenv(key.c_str());
    if (!value)
        return {};
    return std::string(value);
}

std::optional<std::string> getEnvNonEmpty(const std::string & key)
{
    auto value = getEnv(key);
    if (value == "")
        return {};
    return value;
}

std::optional<OsString> getEnvOsNonEmpty(const OsString & key)
{
    auto value = getEnvOs(key);
    if (value == OS_STR(""))
        return {};
    return value;
}

void clearEnv()
{
    for (auto & [name, value] : getEnvOs())
        unsetEnvOs(name.c_str());
}

void replaceEnv(const StringMap & newEnv)
{
    clearEnv();
    for (auto & newEnvVar : newEnv)
        setEnv(newEnvVar.first.c_str(), newEnvVar.second.c_str());
}

} // namespace nix
