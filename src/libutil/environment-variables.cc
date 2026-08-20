#include "nix/util/environment-variables.hh"

namespace nix {

std::optional<std::string> getEnv(const std::string & key)
{
#ifdef _WIN32
    /* Windows keeps two environments: the Win32 process environment block, and
       the C runtime's own copy, snapshotted at process start. `setEnv()` and
       `setEnvOs()` write the Win32 block via `SetEnvironmentVariable`, which
       does not update the CRT copy -- so `getenv()` cannot see anything this
       process has set, and this accessor would report a variable absent that
       `getEnvOs()` reports present. Read the Win32 block, as the rest of the
       Windows environment layer already does.

       Note the platform asymmetry this restores: on Unix `getEnvOs()` delegates
       to this function, because there the CRT is the primitive. On Windows the
       primitive is the Win32 block, so the delegation has to run the other way. */
    auto value = getEnvOs(string_to_os_string(key));
    if (!value)
        return {};
    return os_string_to_string(*value);
#else
    char * value = getenv(key.c_str());
    if (!value)
        return {};
    return std::string(value);
#endif
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
