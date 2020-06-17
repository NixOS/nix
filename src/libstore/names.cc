#include "names.hh"
#include "versions.hh"


namespace nix {


DrvName::DrvName()
{
    name = "";
}


/* Parse a derivation name.  The `name' part of a derivation name is
   everything up to but not including the first dash *not* followed by
   a letter.  The `version' part is the rest (excluding the separating
   dash).  E.g., `apache-httpd-2.0.48' is parsed to (`apache-httpd',
   '2.0.48'). */
DrvName::DrvName(std::string_view s) : hits(0)
{
    name = fullName = std::string(s);
    for (unsigned int i = 0; i < s.size(); ++i) {
        /* !!! isalpha/isdigit are affected by the locale. */
        if (s[i] == '-' && i + 1 < s.size() && !isalpha(s[i + 1])) {
            name = s.substr(0, i);
            version = s.substr(i + 1);
            break;
        }
    }
}


bool DrvName::matches(DrvName & n)
{
    if (name != "*") {
        if (!regex) regex = std::unique_ptr<std::regex>(new std::regex(name, std::regex::extended));
        if (!std::regex_match(n.name, *regex)) return false;
    }
    if (version != "" && version != n.version) return false;
    return true;
}


DrvNames drvNamesFromArgs(const Strings & opArgs)
{
    DrvNames result;
    for (auto & i : opArgs)
        result.push_back(DrvName(i));
    return result;
}


}
