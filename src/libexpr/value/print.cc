#include "value/print.hh"

namespace nix {

std::ostream &
printLiteral(std::ostream & str, const std::string_view string) 
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
printLiteral(std::ostream & str, bool boolean) 
{
    str << (boolean ? "true" : "false");
    return str;
}

}
