#pragma once
/**
 * @file
 * @brief Common printing functions for the Nix language
 *
 * While most types come with their own methods for printing, they share some
 * functions that are placed here.
 */

#include <iostream>

#include "fmt.hh"
#include "print-options.hh"

namespace nix {

class EvalState;
struct Value;

/**
 * Print a string as a Nix string literal.
 *
 * Quotes and fairly minimal escaping are added.
 *
 * @param o The output stream to print to
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

void printValue(EvalState & state, std::ostream & str, Value & v, PrintOptions options = PrintOptions {});

/**
 * A partially-applied form of `printValue` which can be formatted using `<<`
 * without allocating an intermediate string.
 */
class ValuePrinter {
    friend std::ostream & operator << (std::ostream & output, const ValuePrinter & printer);
private:
    EvalState & state;
    Value & value;
    PrintOptions options;

public:
    ValuePrinter(EvalState & state, Value & value, PrintOptions options = PrintOptions {})
        : state(state), value(value), options(options) { }
};

std::ostream & operator<<(std::ostream & output, const ValuePrinter & printer);


/**
 * `ValuePrinter` does its own ANSI formatting, so we don't color it
 * magenta.
 */
template<>
HintFmt & HintFmt::operator%(const ValuePrinter & value);

}
