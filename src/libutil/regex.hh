#pragma once

#include "types.hh"

#include <sys/types.h>
#include <regex.h>

namespace nix {

class Regex
{
public:
    Regex(const string & pattern);
    ~Regex();
    bool matches(const string & s);

private:
    regex_t preg;
    string showError(int err);
};

}
