#pragma once

#include "types.hh"
#include <string>
#include <optional>
#include <iostream>

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

class ErrorInfo;

// -------------------------------------------------
// forward declarations before ErrLine.
template <class T> 
class AddLineNumber;

template <class T> 
class AddColumnRange;

template <class T> 
class AddLOC;

class ErrLine { 
  public:
    int lineNumber;
    optional<ColumnRange> columnRange;
    optional<string> prevLineOfCode;
    string errLineOfCode;
    optional<string> nextLineOfCode;

    friend AddLineNumber<ErrLine>;
    friend AddColumnRange<ErrLine>;
    friend AddLOC<ErrLine>;
    ErrLine& GetEL() { return *this; }
  private:
    ErrLine() {}
};

template <class T> 
class AddLineNumber : public T
{
  public:
    T& lineNumber(int lineNumber) { 
      GetEL().lineNumber = lineNumber;
      return *this;
    }
  protected:
    ErrLine& GetEL() { return T::GetEL(); }
};

template <class T> 
class AddColumnRange : public T
{
  public:
    T& columnRange(unsigned int start, unsigned int len) { 
      GetEL().columnRange = { start, len };
      return *this;
    }
  protected:
    ErrLine& GetEL() { return T::GetEL(); }
};

template <class T> 
class AddLOC : public T
{
  public:
    T& linesOfCode(optional<string> prevloc, string loc, optional<string> nextloc) { 
      GetEL().prevLineOfCode = prevloc;
      GetEL().errLineOfCode = loc;
      GetEL().nextLineOfCode = nextloc;
      return *this;
    }
  protected:
    ErrLine& GetEL() { return T::GetEL(); }
};

typedef AddLineNumber<AddColumnRange<AddLOC<ErrLine>>> MkErrLine;
MkErrLine mkErrLine;


// -------------------------------------------------
// NixCode.

template <class T>
class AddNixFile;

template <class T>
class AddErrLine;

class NixCode { 
  public:
    optional<string> nixFile;
    optional<ErrLine> errLine;

    friend AddNixFile<NixCode>;
    friend AddErrLine<NixCode>;
    friend ErrorInfo;
    NixCode& GetNC() { return *this; }
  private:
    NixCode() {}
};

template <class T> 
class AddNixFile : public T
{
  public:
    T& nixFile(string filename) { 
      GetNC().nixFile = filename;
      return *this;
    }
  protected:
    NixCode& GetNC() { return T::GetNC(); }
};

template <class T> 
class AddErrLine : public T
{
  public:
    T& errLine(ErrLine errline) { 
      GetNC().errLine = errline;
      return *this;
    }
  protected:
    NixCode& GetNC() { return T::GetNC(); }
};

typedef AddNixFile<AddErrLine<NixCode>> MkNixCode;

// -------------------------------------------------
// ErrorInfo.

template <class T>
class AddName;

template <class T>
class AddDescription;

template <class T>
class AddNixCode;

class ErrorInfo { 
  public:
    ErrLevel level;
    string name;
    string description;
    string program;
    optional<NixCode> nixCode;
    string hint;
    ErrorInfo& GetEI() { return *this; }

    // give these access to the private constructor, 
    // when they are direct descendants.
    friend AddName<ErrorInfo>;
    friend AddDescription<ErrorInfo>;
    friend AddNixCode<ErrorInfo>;
    
  protected:
    ErrorInfo(ErrLevel level) { this->level = level; }
};

class EIError : public ErrorInfo
{
  protected:
    EIError() : ErrorInfo(elError) {}
};

class EIWarning : public ErrorInfo
{
  protected:
    EIWarning() : ErrorInfo(elWarning) {}
};

template <class T>
class AddName : private T
{
  public:
    T& name(const std::string &name){
      GetEI().name = name;
      return *this;
    }
  protected:
    ErrorInfo& GetEI() { return T::GetEI(); }
};

template <class T>
class AddDescription : private T 
{
  public:
    T& description(const std::string &description){
      GetEI().description = description;
      return *this;
    }
  protected:
    ErrorInfo& GetEI() { return T::GetEI(); }
};

template <class T>
class AddNixCode : private T 
{
  public:
    T& nixcode(const NixCode &nixcode){
      GetEI().nixCode = nixcode;
      return *this;
    }
  protected:
    ErrorInfo& GetEI() { return T::GetEI(); }
};

typedef AddName<AddDescription<EIError>> StandardError;
typedef AddName<AddDescription<EIWarning>> StandardWarning;

typedef AddName<AddDescription<AddNixCode<EIError>>> MkNixError;
typedef AddName<AddDescription<AddNixCode<EIWarning>>> MkNixWarning;

string showErrLine(ErrLine &errLine);

void print_code_lines(string &prefix, NixCode &nix_code); 

void print_error(ErrorInfo &einfo);
}

