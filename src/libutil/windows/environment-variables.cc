#include "environment-variables.hh"

#include "processenv.h"

namespace nix {

int unsetenv(const char *name)
{
    return -SetEnvironmentVariableA(name, nullptr);
}

int setEnv(const char * name, const char * value)
{
    return -SetEnvironmentVariableA(name, value);
}

}
