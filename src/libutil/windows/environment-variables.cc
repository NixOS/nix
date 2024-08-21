#include "environment-variables.hh"

#include "processenv.h"

namespace nix {

std::optional<OsString> getEnvOs(const OsString & key)
{
    // Determine the required buffer size for the environment variable value
    DWORD bufferSize = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (bufferSize == 0) {
        return std::nullopt;
    }

    // Allocate a buffer to hold the environment variable value
    std::wstring value{L'\0', bufferSize};

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

}
