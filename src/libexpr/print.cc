#include "print.hh"

#include <algorithm>
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

// Returns 'true' if the character is a symbol that can't be used in a variable name.
bool isSymbolProhibited(const char & symbol) {
    bool symbolAllowed =
        ((symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z') ||
         (symbol >= '0' && symbol <= '9') || symbol == '_' || symbol == '\'' ||
         symbol == '-');
    return !symbolAllowed;
}

std::ostream &
printIdentifier(std::ostream & str, std::string_view s) {
    if (s.empty()) {
        str << "\"\"";
        return str;
    }

    if (isReservedKeyword(s)) {
        str << '"' << s << '"';
        return str;
    }

    char firstSymbol = s[0];
    // Name can only begin with a letter or an underscore.
    if (!((firstSymbol >= 'a' && firstSymbol <= 'z') ||
          (firstSymbol >= 'A' && firstSymbol <= 'Z') || firstSymbol == '_')) {
        printLiteralString(str, s);
        return str;
    }
    // Name cannot contain prohibited symbols.
    if (std::any_of(std::begin(s), std::end(s), isSymbolProhibited)) {
        printLiteralString(str, s);
        return str;
    }

    str << s;
    return str;
}

static bool isVarName(std::string_view s)
{
    if (s.empty()) return false;
    if (isReservedKeyword(s)) return false;
    char firstSymbol = s[0];
    // Name cannot begin with a digit or a special character.
    if ((firstSymbol >= '0' && firstSymbol <= '9') || firstSymbol == '-' ||
        firstSymbol == '\'')
        return false;

    return std::none_of(std::begin(s), std::end(s), isSymbolProhibited);
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
