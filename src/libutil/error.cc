#include "error.hh"

#include <iostream>
#include <optional>

namespace nix {

using std::cout;
using std::endl;
using std::nullopt;

optional<string> ErrorInfo::programName = nullopt;

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

void printCodeLines(string &prefix, NixCode &nixCode) 
{
  
  if (nixCode.errLine.has_value()) 
  {
    // previous line of code.
    if (nixCode.errLine->prevLineOfCode.has_value()) { 
      cout << format("%1% %|2$5d|| %3%")
        %  prefix
        % (nixCode.errLine->lineNumber - 1)
        % *nixCode.errLine->prevLineOfCode
        << endl;
    }

    // line of code containing the error.%2$+5d%
    cout << format("%1% %|2$5d|| %3%")
      %  prefix
      % (nixCode.errLine->lineNumber)
      % nixCode.errLine->errLineOfCode
      << endl;
    
    // error arrows for the column range.
    if (nixCode.errLine->columnRange.has_value()) 
    {
      int start = nixCode.errLine->columnRange->start;
      std::string spaces;
      for (int i = 0; i < start; ++i)
      {
        spaces.append(" ");
      }

      int len = nixCode.errLine->columnRange->len;
      std::string arrows;
      for (int i = 0; i < len; ++i)
      {
        arrows.append("^");
      }
    
      cout << format("%1%      |%2%" ANSI_RED "%3%" ANSI_NORMAL) % prefix % spaces % arrows << endl;
    }



    // next line of code.
    if (nixCode.errLine->nextLineOfCode.has_value()) { 
      cout << format("%1% %|2$5d|| %3%")
      %  prefix
      % (nixCode.errLine->lineNumber + 1)
      % *nixCode.errLine->nextLineOfCode
      << endl;
    }

  }
    
}

void printErrorInfo(ErrorInfo &einfo) 
{
  int errwidth = 80;
  string prefix = "  ";

  string levelString;
  switch (einfo.level) 
  {
    case ErrLevel::elError: 
      {
        levelString = ANSI_RED;
        levelString += "error:";
        levelString += ANSI_NORMAL;
        break;
      }
    case ErrLevel::elWarning: 
      {
        levelString = ANSI_YELLOW;
        levelString += "warning:";  
        levelString += ANSI_NORMAL;
        break;
      }
    default: 
      {
        levelString = format("invalid error level: %1%") % einfo.level;  
        break;
      }
  }

  int ndl = prefix.length() + levelString.length() + 3 + einfo.name.length() + einfo.programName.value_or("").length();
  int dashwidth = ndl > (errwidth - 3) ? 3 : errwidth - ndl; 

  string dashes;
  for (int i = 0; i < dashwidth; ++i)
    dashes.append("-");

  // divider.
  cout << format("%1%%2%" ANSI_BLUE " %3% %4% %5% %6%" ANSI_NORMAL)
    % prefix
    % levelString
    % "---"
    % einfo.name
    % dashes
    % einfo.programName.value_or("")
    << endl;

  // filename.
  if (einfo.nixCode.has_value()) 
  {
    if (einfo.nixCode->nixFile.has_value()) 
    {
      string eline = einfo.nixCode->errLine.has_value()  
        ? string(" ") + showErrLine(*einfo.nixCode->errLine)
        : "";

      cout << format("%1%in file: " ANSI_BLUE "%2%%3%" ANSI_NORMAL) 
        % prefix % *einfo.nixCode->nixFile % eline << endl;
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
    printCodeLines(prefix, *einfo.nixCode);

  // hint
  if (einfo.hint.has_value()) 
  {
    cout << prefix << endl;
    cout << prefix << *einfo.hint << endl;
    cout << prefix << endl;
  }
}

}
