#include "regex.hh"
#include "types.hh"

#include <algorithm>

namespace nix {

Regex::Regex(const string & pattern, bool subs)
{
    /* Patterns must match the entire string. */
    int err = regcomp(&preg, ("^(" + pattern + ")$").c_str(), (subs ? 0 : REG_NOSUB) | REG_EXTENDED);
    if (err) throw RegexError(format("compiling pattern ‘%1%’: %2%") % pattern % showError(err));
    nrParens = subs ? std::count(pattern.begin(), pattern.end(), '(') : 0;
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

bool Regex::matches(const string & s, Subs & subs)
{
    regmatch_t pmatch[nrParens + 2];
    int err = regexec(&preg, s.c_str(), nrParens + 2, pmatch, 0);
    if (err == 0) {
        for (unsigned int n = 2; n < nrParens + 2; ++n)
            if (pmatch[n].rm_eo != -1)
                subs[n - 2] = string(s, pmatch[n].rm_so, pmatch[n].rm_eo - pmatch[n].rm_so);
        return true;
    }
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
