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

  /*
  ColumnRange columnRange;
  columnRange.start = 24;
  columnRange.len = 14;

  ErrLine errline;
  errline.lineNumber = 7;
  errline.columnRange = optional(columnRange);
  errline.errLineOfCode = "line of code where the error occurred";

  NixCode nixcode;
  nixcode.nixFile = optional("myfile.nix");
  nixcode.errLine = errline; 
  
  ErrorInfo generic;
  generic.level = elError;
  generic.name = "error name";
  generic.description = "general error description";
  generic.program = "nixtool.exe";
  generic.nixCode = nixcode;

  printErrorInfo(generic);
  */

  printErrorInfo(StandardError()
                .name("name")
                .description("error description")
                .nohint()
                );

  printErrorInfo(StandardWarning()
                .name("warning name")
                .description("warning description")
                .nohint()
                );


  printErrorInfo(MkNixWarning()
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

  printErrorInfo(MkNixError()
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
