#include "regex.hh"
#include "types.hh"

namespace nix {

Regex::Regex(const string & pattern)
{
    /* Patterns must match the entire string. */
    int err = regcomp(&preg, ("^(" + pattern + ")$").c_str(), REG_NOSUB | REG_EXTENDED);
    if (err) throw Error(format("compiling pattern ‘%1%’: %2%") % pattern % showError(err));
}

Regex::~Regex()
{
    regfree(&preg);
}

bool Regex::matches(const string & s)
{
    int err = regexec(&preg, s.c_str(), 0, 0, 0);
    if (err == 0) return true;
    else if (err == REG_NOMATCH) return false;
    throw Error(format("matching string ‘%1%’: %2%") % s % showError(err));
}

string Regex::showError(int err)
{
    char buf[256];
    regerror(err, &preg, buf, sizeof(buf));
    return string(buf);
}

}
