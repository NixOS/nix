#pragma once
///@file

#include <sstream>
#include <string>
#include <string_view>

#include "nix/expr/print.hh"

namespace nix {

/**
 * Find the position of the last `.` in `s` that is not inside a quoted
 * attribute (i.e. not between an unescaped opening `"` and its close).
 * Returns std::string::npos when no unquoted dot exists.
 */
inline size_t findLastUnquotedDot(const std::string & s)
{
    size_t lastDot = std::string::npos;
    bool inQuote = false;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"')
            inQuote = !inQuote;
        else if (s[i] == '.' && !inQuote)
            lastDot = i;
    }
    return lastDot;
}

/**
 * Format an attribute name for use in a completion string.  Names that
 * are valid identifiers are returned bare; everything else is wrapped in
 * double quotes (using `printAttributeName`).
 */
inline std::string formatAttrName(std::string_view name)
{
    std::ostringstream ss;
    printAttributeName(ss, name);
    return ss.str();
}

} // namespace nix
