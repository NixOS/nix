#include "../../src/libutil/error.hh"

#include <optional>
#include <iostream>

int main() 
{
    using namespace nix; 

    // In each program where errors occur, this has to be set.
    ErrorInfo::programName = std::optional("error-test");

    // There are currently four error types:
    //  
    // 			ProgramError, ProgramWarning, NixLangError, NixLangWarning.
    // 			
    // Each error type is created with a specific sequence of builder functions.
    // Unlike with a constructor, each parameter is clearly named.
    // If the sequence of function calls isn't followed, then there's a type error.
    // This should make for a consistent look in the code when errors are created.

    // ProgramError takes name, description, and an optional hint.
    printErrorInfo(
        ProgramError()
            .name("name")
            .description("error description")
            .nohint()
            );

    // ProgramWarning takes name, description, and an optional hint.
    // The hint is in the form of a hintfmt class, which wraps boost::format(), and 
    // makes all the substituted text yellow.
    printErrorInfo(
        ProgramWarning()
            .name("warning name")
            .description("warning description")
            .hint(hintfmt("there was a %1%") % "warning") // 'warning' will be yellow.
            );

    /*
      // some invalid errors:
      
      // type error: no hint function.
      ProgramError()
        .name("name")
        .description("error description");

      // type error:  description before name.
      ProgramError()
        .description("error description")
        .name("name")
        .nohint();

      // type error: hint function with regular boost format, not special hintfmt.
      ProgramError()
        .description("error description")
        .name("name")
        .hint(format("there was a %1%") % "warning");
    */

    // NixLangWarning adds nix file, line number, column range, and the lines of code 
    // where a warning occurred.
    printErrorInfo(
        NixLangWarning()
            .name("warning name")
            .description("warning description")
            .nixFile("myfile.nix")
            .lineNumber(40)
            .columnRange(13,7)
            .linesOfCode(std::nullopt
                        ,"this is the problem line of code"
                        ,std::nullopt)
            .hint(hintfmt("this hint has %1% templated %2%!!") % "yellow" % "values")
            );

    // NixLangError is just the same as NixLangWarning, except for the Error flag.
    printErrorInfo(
        NixLangError()
            .name("error name")
            .description("error description")
            .nixFile("myfile.nix")
            .lineNumber(40)
            .columnRange(13,7)
            .linesOfCode(std::optional("previous line of code")
                        ,"this is the problem line of code"
                        ,std::optional("next line of code"))
            .hint(hintfmt("this hint has %1% templated %2%!!") % "yellow" % "values")
            );

    return 0;
}
