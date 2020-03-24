#include "error.hh"

#include <iostream>

namespace nix {

using std::cout;
using std::endl;

// return basic_format?
string showErrLine(ErrLine &errLine)
{
  if (errLine.columnRange.has_value()) 
  {
    return (format("(%1%:%2%)") % errLine.lineNumber % errLine.columnRange->start).str();
  }
  else
  {
    return (format("(%1%)") % errLine.lineNumber).str();
  };
}

void print_code_lines(string &prefix, NixCode &nix_code) 
{
  
  if (nix_code.errLine.has_value()) 
  {
    // previous line of code.
    if (nix_code.errLine->prevLineOfCode.has_value()) { 
      cout << format("%1% %|2$5d|| %3%")
        %  prefix
        % (nix_code.errLine->lineNumber - 1)
        % *nix_code.errLine->prevLineOfCode
        << endl;
    }

    // line of code containing the error.%2$+5d%
    cout << format("%1% %|2$5d|| %3%")
      %  prefix
      % (nix_code.errLine->lineNumber)
      % nix_code.errLine->errLineOfCode
      << endl;
    
    // error arrows for the column range.
    if (nix_code.errLine->columnRange.has_value()) 
    {
      int start = nix_code.errLine->columnRange->start;
      std::string spaces;
      for (int i = 0; i < start; ++i)
      {
        spaces.append(" ");
      }

      int len = nix_code.errLine->columnRange->len;
      std::string arrows;
      for (int i = 0; i < len; ++i)
      {
        arrows.append("^");
      }
    
      cout << format("%1%      |%2%%3%") % prefix % spaces % arrows;
    }



    // next line of code.
    if (nix_code.errLine->nextLineOfCode.has_value()) { 
      cout << format("%1% %|2$5d|| %3%")
      %  prefix
      % (nix_code.errLine->lineNumber + 1)
      % *nix_code.errLine->nextLineOfCode
      << endl;
    }

  }
    
}

void print_error(ErrorInfo &einfo) 
{
  int errwidth = 80;
  string prefix = "  ";

  string level_string;
  switch (einfo.level) 
  {
    case ErrLevel::elError: 
      {
        level_string = "error:";  // TODO make red.
        break;
      }
    case ErrLevel::elWarning: 
      {
        level_string = "warning:";  // TODO make yellow.
        break;
      }
  }

  int ndl = level_string.length() + 3 + einfo.name.length() + einfo.program.length();
  int dashwidth = errwidth - 3 ? 3 : 80 - ndl; 

  string dashes;
  for (int i = 0; i < dashwidth; ++i)
    dashes.append("-");

  // divider.
  cout << format("%1%%2% %3% %4% %5% %6%")
    % prefix
    % level_string
    % "---"
    % einfo.name
    % dashes
    % einfo.program
    << endl;

  // filename.
  if (einfo.nixCode.has_value()) 
  {
    if (einfo.nixCode->nixFile.has_value()) 
    {
      string eline = einfo.nixCode->errLine.has_value()  
        ? string(" ") + showErrLine(*einfo.nixCode->errLine)
        : "";

      cout << format("%1%in file: %2%%3%") % prefix % *einfo.nixCode->nixFile % eline << endl;
      cout << prefix << endl;
    }
    else
    {
      cout << format("%1%from command line argument") % prefix << endl;
      cout << prefix << endl;
    }
  }

  // description
  cout << prefix << einfo.description << endl;
  cout << prefix << endl;

  // lines of code.
  if (einfo.nixCode.has_value())
    print_code_lines(prefix, *einfo.nixCode);

  // hint
  cout << prefix << einfo.hint << endl;
  cout << prefix << endl;

}

}
