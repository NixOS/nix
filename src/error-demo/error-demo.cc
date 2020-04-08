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
        ErrorInfo { .level = elError,
                    .name = "name",
                    .description = "error description",
                  });

    // ProgramWarning takes name, description, and an optional hint.
    // The hint is in the form of a hintfmt class, which wraps boost::format(),
    // and makes all the substituted text yellow.
    printErrorInfo(
        ErrorInfo { .level = elWarning,
                    .name = "name",
                    .description = "error description",
                    .hint =  std::optional(
                                 hintfmt("there was a %1%", "warning")),
                  });


    // NixLangWarning adds nix file, line number, column range, and the lines of
    // code where a warning occurred.
    SymbolTable testTable;
    auto problem_file = testTable.create("myfile.nix");

    printErrorInfo(
        ErrorInfo{
            .level = elWarning,
            .name = "warning name",
            .description = "warning description",
            .hint = hintfmt("this hint has %1% templated %2%!!", "yellow", "values"),
            .nixCode = NixCode {
                .errPos = Pos(problem_file, 40, 13),
                .prevLineOfCode = std::nullopt,
                .errLineOfCode = "this is the problem line of code",
                .nextLineOfCode = std::nullopt
            }});

    // NixLangError is just the same as NixLangWarning, except for the Error
    // flag.
    printErrorInfo(
        ErrorInfo{
            .level = elError,
            .name = "error name",
            .description = "error description",
            .hint = hintfmt("this hint has %1% templated %2%!!", "yellow", "values"),
            .nixCode = NixCode {
                .errPos = Pos(problem_file, 40, 13),
                .prevLineOfCode = std::optional("previous line of code"),
                .errLineOfCode = "this is the problem line of code",
                .nextLineOfCode = std::optional("next line of code"),
            }});


    return 0;
}
