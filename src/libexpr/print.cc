#include "print.hh"

namespace nix {

std::ostream &
printLiteralString(std::ostream & str, const std::string_view string)
{
    str << "\"";
    for (auto i = string.begin(); i != string.end(); ++i) {
        if (*i == '\"' || *i == '\\') str << "\\" << *i;
        else if (*i == '\n') str << "\\n";
        else if (*i == '\r') str << "\\r";
        else if (*i == '\t') str << "\\t";
        else if (*i == '$' && *(i+1) == '{') str << "\\" << *i;
        else str << *i;
    }
    str << "\"";
    return str;
}

std::ostream &
printLiteralBool(std::ostream & str, bool boolean)
{
    str << (boolean ? "true" : "false");
    return str;
}

std::ostream &
printIdentifier(std::ostream & str, std::string_view s) {
    if (s.empty())
        str << "\"\"";
    else if (s == "if") // FIXME: handle other keywords
        str << '"' << s << '"';
    else {
        char c = s[0];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
            printLiteralString(str, s);
            return str;
        }
        for (auto c : s)
            if (!((c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') ||
                  c == '_' || c == '\'' || c == '-')) {
                printLiteralString(str, s);
                return str;
            }
        str << s;
    }
    return str;
}

// FIXME: keywords
static bool isVarName(std::string_view s)
{
    if (s.size() == 0) return false;
    char c = s[0];
    if ((c >= '0' && c <= '9') || c == '-' || c == '\'') return false;
    for (auto & i : s)
        if (!((i >= 'a' && i <= 'z') ||
              (i >= 'A' && i <= 'Z') ||
              (i >= '0' && i <= '9') ||
              i == '_' || i == '-' || i == '\''))
            return false;
    return true;
}

std::ostream &
printAttributeName(std::ostream & str, std::string_view name) {
    if (isVarName(name))
        str << name;
    else
        printLiteralString(str, name);
    return str;
}


}
