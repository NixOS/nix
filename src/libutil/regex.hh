#pragma once

#include "types.hh"

#include <sys/types.h>
#include <regex.h>

#include <map>

namespace nix {

MakeError(RegexError, Error)

class Regex
{
public:
    Regex(const string & pattern, bool subs = false);
    ~Regex();
    bool matches(const string & s);
    typedef std::map<unsigned int, string> Subs;
    bool matches(const string & s, Subs & subs);

private:
    unsigned nrParens;
    regex_t preg;
    string showError(int err);
};

}
