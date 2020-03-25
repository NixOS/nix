#include "error.hh"

#include <optional>
#include <iostream>

using std::optional;
using std::nullopt;
using std::cout;
using std::endl;

int main() {

using namespace nix; 

  ErrorInfo::programName = optional("errorTest");

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

  print_error(StandardError()
                .name("name")
                .description("description"));

  print_error(StandardWarning()
                .name("warning name")
                .description("warning description"));


  print_error(MkNixWarning()
                .name("warning name")
                .description("warning description")
                .nixFile("myfile.nix")
                .lineNumber(40)
                .columnRange(13,7)
                .linesOfCode(nullopt
                            ,"this is the problem line of code"
                            ,nullopt));

  print_error(MkNixError()
                .name("error name")
                .description("error description")
                .nixFile("myfile.nix")
                .lineNumber(40)
                .columnRange(13,7)
                .linesOfCode(nullopt
                            ,"this is the problem line of code"
                            ,nullopt));

  return 0;
}




