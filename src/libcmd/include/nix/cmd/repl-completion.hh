#pragma once
///@file

#include <set>
#include <sstream>
#include <string>
#include <string_view>

#include "nix/expr/print.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"

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

/**
 * Given a dotted attribute path prefix (e.g. "foo.ba") and the set of
 * attribute names available at the innermost attrset, return the
 * completions that match.  This is the pure string-matching core of
 * REPL tab-completion for attribute paths — it does not require
 * EvalState.
 *
 * @param dottedPrefix  The portion of user input after the last
 *   whitespace/bracket, containing at least one unquoted dot.
 *   Example: "pkgs.li", "a.\"test"
 * @param attrNames  The attribute names present in the attrset that
 *   the expression before the last dot evaluates to.
 * @return  Completion strings (including the full dotted path prefix).
 */
inline StringSet matchAttrCompletions(const std::string & dottedPrefix, const StringSet & attrNames)
{
    StringSet completions;

    size_t dot = findLastUnquotedDot(dottedPrefix);
    if (dot == std::string::npos) {
        /* No unquoted dot — not an attribute path, nothing to complete. */
        return completions;
    }

    auto expr = dottedPrefix.substr(0, dot);
    auto cur2 = dottedPrefix.substr(dot + 1);

    /* If the user started typing a quoted attribute name
       (e.g. `foo."bar`), strip the opening quote so we can
       match against the raw attribute names. */
    bool insideQuote = false;
    if (!cur2.empty() && cur2[0] == '"') {
        cur2 = cur2.substr(1);
        insideQuote = true;
    }

    for (const auto & name : attrNames) {
        if (std::string_view(name).substr(0, cur2.size()) != cur2)
            continue;
        auto formattedName = formatAttrName(name);
        if (insideQuote) {
            /* The user already typed an opening `"`, so the
               completion string must include it to keep the
               character offsets aligned with the input buffer.
               `formattedName` already wraps names that need
               quoting in `"…"`, so we can use it directly for
               those.  For plain identifiers that the user
               gratuitously quoted, wrap them ourselves. */
            if (formattedName.size() >= 2 && formattedName[0] == '"')
                completions.insert(concatStrings(expr, ".", formattedName));
            else
                completions.insert(concatStrings(expr, ".\"", formattedName, "\""));
        } else {
            completions.insert(concatStrings(expr, ".", formattedName));
        }
    }

    return completions;
}

} // namespace nix
