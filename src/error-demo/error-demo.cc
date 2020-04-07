#include "error.hh"
#include "nixexpr.hh"

#include <iostream>
#include <optional>

int main()
{
    using namespace nix;

    // In each program where errors occur, this has to be set.
    ErrorInfo::programName = std::optional("error-demo");

    // There are currently four constructor functions:
    //
    // 			ProgramError, ProgramWarning, NixLangError, NixLangWarning.
    //

    // ProgramError takes name, description, and an optional hint.
    printErrorInfo(
        ErrorInfo::ProgramError("name",
                                "error description",
                                std::nullopt));

    // ProgramWarning takes name, description, and an optional hint.
    // The hint is in the form of a hintfmt class, which wraps boost::format(),
    // and makes all the substituted text yellow.
    printErrorInfo(
        ErrorInfo::ProgramWarning("name",
                                  "warning description",
                                  std::optional(
                                      hintfmt("there was a %1%", "warning"))));

    // NixLangWarning adds nix file, line number, column range, and the lines of
    // code where a warning occurred.
    SymbolTable testTable;
    auto problem_symbol = testTable.create("problem");

    printErrorInfo(
        ErrorInfo::NixLangWarning(
            "warning name",
            "warning description",
            Pos(problem_symbol, 40, 13),
            std::nullopt,
            "this is the problem line of code",
            std::nullopt,
            hintfmt("this hint has %1% templated %2%!!", "yellow", "values")));

    // NixLangError is just the same as NixLangWarning, except for the Error
    // flag.
    printErrorInfo(
        ErrorInfo::NixLangError(
            "error name",
            "error description",
            Pos(problem_symbol, 40, 13),
            std::optional("previous line of code"),
            "this is the problem line of code",
            std::optional("next line of code"),
            hintfmt("this hint has %1% templated %2%!!", "yellow", "values")));

    return 0;
}
