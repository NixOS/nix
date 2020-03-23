#pragma once

#include "types.hh"
#include <string>
#include <optional>

using std::string;
using std::optional;

namespace nix {

enum ErrLevel 
  { elWarning
  , elError
  };

class ColumnRange { 
  public:
  unsigned int start;
  unsigned int len;
};

class ErrLine { 
  public:
  int lineNumber;
  optional<ColumnRange> columnRange;
  optional<string> prevLineOfCode;
  string errLineOfCode;
  optional<string> nextLineOfCode;
};

class NixCode { 
  public:
  optional<string> nixFile;
  optional<ErrLine> errLine;
};


class ErrorInfo { 
  public:
  ErrLevel level;
  string errName;
  string description;
  string toolName;
  optional<NixCode> nixCode;
  string hint;
};

string showErrLine(ErrLine &errLine);

void print_code_lines(string &prefix, NixCode &nix_code); 

void print_error(ErrorInfo &einfo);
}

