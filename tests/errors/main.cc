#include "error.hh"

#include <optional>
#include <iostream>

using std::optional;
using std::nullopt;

int main() {

using namespace nix; 

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

  print_error(generic);
  */

  StandardError standardError;

  print_error(standardError
                .name("name")
                .description("description"));

  StandardWarning standardWarning;

  print_error(standardWarning
                .name("warning name")
                .description("warning description"));

  print_error(MkNixError()
                .name("name")
                .description("description")
                .nixcode(
                  MkNixCode()
                    .nixFile("myfile.nix")
                    .errLine(MkErrLine().lineNumber(40)
                           .columnRange(13,7)
                           .linesOfCode(nullopt
                                      ,"this is the problem line of code"
                                      ,nullopt))));

  print_error(MkNixWarning()
                .name("name")
                .description("description")
                .nixcode(
                  MkNixCode()
                    .nixFile("myfile.nix")
                    .errLine(MkErrLine().lineNumber(40)
                           .columnRange(13,7)
                           .linesOfCode(nullopt
                                      ,"this is the problem line of code"
                                      ,nullopt))));

  return 0;
}




