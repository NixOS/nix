#include "nix/util/environment-variables.hh"

#ifdef _WIN32
#  include "processenv.h"

namespace nix {

std::optional<OsString> getEnvOs(const OsString & key)
{
    // Determine the required buffer size for the environment variable value
    DWORD bufferSize = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (bufferSize == 0) {
        return std::nullopt;
    }

    /* Allocate a buffer to hold the environment variable value.
       WARNING: Do not even think about using uniform initialization here,
       we DONT want to call the initializer list ctor accidentally. */
    std::wstring value(bufferSize, L'\0');

    // Retrieve the environment variable value
    DWORD resultSize = GetEnvironmentVariableW(key.c_str(), &value[0], bufferSize);
    if (resultSize == 0) {
        return std::nullopt;
    }

    // Resize the string to remove the extra null characters
    value.resize(resultSize);

    return value;
}

int unsetenv(const char * name)
{
    return -SetEnvironmentVariableA(name, nullptr);
}

int setEnv(const char * name, const char * value)
{
    return -SetEnvironmentVariableA(name, value);
}

int setEnvOs(const OsString & name, const OsString & value)
{
    return -SetEnvironmentVariableW(name.c_str(), value.c_str());
}

} // namespace nix
#endif
