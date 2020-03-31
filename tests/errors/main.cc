#include "error.hh"

#include <optional>
#include <iostream>

using std::optional;
using std::nullopt;
using std::cout;
using std::endl;

int main() 
{
  using namespace nix; 

  ErrorInfo::programName = optional("error-test");

  printErrorInfo(ProgramError()
                .name("name")
                .description("error description")
                .nohint()
                );

  printErrorInfo(ProgramWarning()
                .name("warning name")
                .description("warning description")
                .nohint()
                );


  printErrorInfo(NixLangWarning()
                .name("warning name")
                .description("warning description")
                .nixFile("myfile.nix")
                .lineNumber(40)
                .columnRange(13,7)
                .linesOfCode(nullopt
                            ,"this is the problem line of code"
                            ,nullopt)
                .hint(hintfmt("this hint has %1% templated %2%!!") % "yellow" % "values")
                );

  printErrorInfo(NixLangError()
                .name("error name")
                .description("error description")
                .nixFile("myfile.nix")
                .lineNumber(40)
                .columnRange(13,7)
                .linesOfCode(optional("previous line of code")
                            ,"this is the problem line of code"
                            ,optional("next line of code"))
                .hint(hintfmt("this hint has %1% templated %2%!!") % "yellow" % "values")
                );

  return 0;
}
