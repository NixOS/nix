#include "util.hh"
#include "environment-variables.hh"

extern char * * environ __attribute__((weak));

namespace nix {

void clearEnv()
{
    for (auto & name : getEnv())
        unsetenv(name.first.c_str());
}

void replaceEnv(const std::map<std::string, std::string> & newEnv)
{
    clearEnv();
    for (auto & newEnvVar : newEnv)
        setenv(newEnvVar.first.c_str(), newEnvVar.second.c_str(), 1);
}

}
