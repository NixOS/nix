#include "environment-variables.hh"

#include "processenv.h"

namespace nix {

<<<<<<< HEAD
int unsetenv(const char *name)
=======
std::optional<OsString> getEnvOs(const OsString & key)
{
    // Determine the required buffer size for the environment variable value
    DWORD bufferSize = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
    if (bufferSize == 0) {
        return std::nullopt;
    }

    // Allocate a buffer to hold the environment variable value
    std::wstring value{bufferSize, L'\0'};

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
>>>>>>> 355f08a72 (Fix argument order in the Windows implementation of `getEnvOs`)
{
    return -SetEnvironmentVariableA(name, nullptr);
}

int setEnv(const char * name, const char * value)
{
    return -SetEnvironmentVariableA(name, value);
}

}
