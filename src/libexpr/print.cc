#include "print.hh"
#include <unordered_set>

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

// Returns `true' is a string is a reserved keyword which requires quotation
// when printing attribute set field names.
//
// This list should generally be kept in sync with `./lexer.l'.
// You can test if a keyword needs to be added by running:
//   $ nix eval --expr '{ <KEYWORD> = 1; }'
// For example `or' doesn't need to be quoted.
bool isReservedKeyword(const std::string_view str)
{
    static const std::unordered_set<std::string_view> reservedKeywords = {
        "if", "then", "else", "assert", "with", "let", "in", "rec", "inherit"
    };
    return reservedKeywords.contains(str);
}

std::ostream &
printIdentifier(std::ostream & str, std::string_view s) {
    if (s.empty())
        str << "\"\"";
    else if (isReservedKeyword(s))
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

static bool isVarName(std::string_view s)
{
    if (s.size() == 0) return false;
    if (isReservedKeyword(s)) return false;
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
