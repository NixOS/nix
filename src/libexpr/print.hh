#pragma once
/**
 * @file
 * @brief Common printing functions for the Nix language
 *
 * While most types come with their own methods for printing, they share some
 * functions that are placed here.
 */

#include <iostream>

namespace nix {
    /**
     * Print a string as a Nix string literal.
     *
     * Quotes and fairly minimal escaping are added.
     *
     * @param s The logical string
     */
    std::ostream & printLiteralString(std::ostream & o, std::string_view s);
    inline std::ostream & printLiteralString(std::ostream & o, const char * s) {
        return printLiteralString(o, std::string_view(s));
    }
    inline std::ostream & printLiteralString(std::ostream & o, const std::string & s) {
        return printLiteralString(o, std::string_view(s));
    }

    /** Print `true` or `false`. */
    std::ostream & printLiteralBool(std::ostream & o, bool b);

    /**
     * Print a string as an attribute name in the Nix expression language syntax.
     *
     * Prints a quoted string if necessary.
     */
    std::ostream & printAttributeName(std::ostream & o, std::string_view s);

    /**
     * Returns `true' is a string is a reserved keyword which requires quotation
     * when printing attribute set field names.
     */
    bool isReservedKeyword(const std::string_view str);

    /**
     * Print a string as an identifier in the Nix expression language syntax.
     *
     * FIXME: "identifier" is ambiguous. Identifiers do not have a single
     *        textual representation. They can be used in variable references,
     *        let bindings, left-hand sides or attribute names in a select
     *        expression, or something else entirely, like JSON. Use one of the
     *        `print*` functions instead.
     */
    std::ostream & printIdentifier(std::ostream & o, std::string_view s);
}
