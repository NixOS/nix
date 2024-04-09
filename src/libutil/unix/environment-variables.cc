#include <cstdlib>

#include "environment-variables.hh"

namespace nix {

int setEnv(const char * name, const char * value)
{
    return ::setenv(name, value, 1);
}

}
