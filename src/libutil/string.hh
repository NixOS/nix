#pragma once

namespace nix {

    /** Locale-independent version of std::islower(). */
    inline bool isASCIILower(char c) { return c >= 'a' && c <= 'z'; };

    /** Locale-independent version of std::isupper(). */
    inline bool isASCIIUpper(char c) { return c >= 'A' && c <= 'Z'; };

    /** Locale-independent version of std::isalpha(). */
    inline bool isASCIIAlpha(char c) { return isASCIILower(c) || isASCIIUpper(c); };

    /** Locale-independent version of std::isdigit(). */
    inline bool isASCIIDigit(char c) { return c >= '0' && c <= '9'; };

}
