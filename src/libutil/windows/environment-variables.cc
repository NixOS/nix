#include "nix/util/environment-variables.hh"

#include <processenv.h>

namespace nix {

/* The primitive on Windows, with `getEnv` delegating here rather than the other
   way round. `getenv` would read the CRT's copy of the environment, snapshotted
   at process start, which `setEnv` does not update, so it cannot see anything
   this process has set. */
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

    /* Resize to remove the extra null characters. A zero count is not a failure
       here: the sizing call already told us the variable exists, so it means the
       value is empty, and resizing to zero returns it as such. */
    value.resize(resultSize);

    return value;
}

OsStringMap getEnvOs()
{
    OsStringMap env;

    auto freeStrings = [](wchar_t * strings) { FreeEnvironmentStringsW(strings); };
    auto envStrings = std::unique_ptr<wchar_t, decltype(freeStrings)>(GetEnvironmentStringsW(), freeStrings);
    auto s = envStrings.get();

    while (true) {
        auto eq = wcschr(s, L'=');
        // Object ends with an empty string, which naturally won't have an =
        if (eq == nullptr)
            break;

        auto value_len = wcslen(eq + 1);

        env[OsString(s, eq - s)] = OsString(eq + 1, value_len);

        // 1 to skip L'=', then value, then 1 to skip L'\0'
        s = eq + 1 + value_len + 1;
    }

    return env;
}

StringMap getEnv()
{
    StringMap env;
    for (auto & [name, value] : getEnvOs()) {
        env.emplace(os_string_to_string(name), os_string_to_string(value));
    }
    return env;
}

int unsetenv(const char * name)
{
    return SetEnvironmentVariableA(name, nullptr) ? 0 : -1;
}

int unsetEnvOs(const OsChar * name)
{
    return SetEnvironmentVariableW(name, nullptr) ? 0 : -1;
}

int setEnv(const char * name, const char * value)
{
    return SetEnvironmentVariableA(name, value) ? 0 : -1;
}

int setEnvOs(const OsString & name, const OsString & value)
{
    return SetEnvironmentVariableW(name.c_str(), value.c_str()) ? 0 : -1;
}

} // namespace nix
