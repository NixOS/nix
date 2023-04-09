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
    std::ostream & printLiteral(std::ostream & o, std::string_view s);
    inline std::ostream & printLiteral(std::ostream & o, const char * s) {
        return printLiteral(o, std::string_view(s));
    }
    inline std::ostream & printLiteral(std::ostream & o, const std::string & s) {
        return printLiteral(o, std::string_view(s));
    }

    /** Print `true` or `false`. */
    std::ostream & printLiteral(std::ostream & o, bool b);
}
