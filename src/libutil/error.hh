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

    ErrLine& ensureErrLine() 
    { 
      if (!this->errLine.has_value())
         this->errLine = optional(ErrLine());
      return *this->errLine; 
    }
};

// -------------------------------------------------
// ErrorInfo.

// Forward friend class declarations.  "builder classes"
template <class T>
class AddName;

template <class T>
class AddDescription;

template <class T>
class AddNixCode;

template <class T>
class AddNixFile;

template <class T>
class AddErrLine;

template <class T> 
class AddLineNumber;

template <class T> 
class AddColumnRange;

template <class T> 
class AddLOC;

// The error info class itself.
class ErrorInfo { 
  public:
    ErrLevel level;
    string name;
    string description;
    optional<NixCode> nixCode;
    string hint;
    ErrorInfo& GetEI() { return *this; }

    static optional<string> programName;

    // give these access to the private constructor, 
    // when they are direct descendants.
    friend AddName<ErrorInfo>;
    friend AddDescription<ErrorInfo>;
    friend AddNixCode<ErrorInfo>;
    friend AddNixFile<ErrorInfo>;
    friend AddErrLine<ErrorInfo>;
    friend AddLineNumber<ErrorInfo>;
    friend AddColumnRange<ErrorInfo>;
    friend AddLOC<ErrorInfo>;
    
    NixCode& ensureNixCode() 
    { 
      if (!this->nixCode.has_value())
         this->nixCode = optional(NixCode());
      return *this->nixCode; 
    }
  protected:
    // constructor is protected, so only the builder classes can create an ErrorInfo.
    ErrorInfo(ErrLevel level) { this->level = level; }


};

// Init as error
class EIError : public ErrorInfo
{
  protected:
    EIError() : ErrorInfo(elError) {}
};

// Init as warning
class EIWarning : public ErrorInfo
{
  protected:
    EIWarning() : ErrorInfo(elWarning) {}
};

// Builder class definitions.
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
class AddNixFile : public T
{
  public:
    T& nixFile(string filename) { 
     GetEI().ensureNixCode().nixFile = filename;
     return *this;
    }
  protected:
    ErrorInfo& GetEI() { return T::GetEI(); }
};

template <class T> 
class AddLineNumber : public T
{
  public:
    T& lineNumber(int lineNumber) { 
      GetEI().ensureNixCode().ensureErrLine().lineNumber = lineNumber;
      return *this;
    }
  protected:
    ErrorInfo& GetEI() { return T::GetEI(); }
};

template <class T> 
class AddColumnRange : public T
{
  public:
    T& columnRange(unsigned int start, unsigned int len) { 
      GetEI().ensureNixCode().ensureErrLine().columnRange = { start, len };
      return *this;
    }
  protected:
    ErrorInfo& GetEI() { return T::GetEI(); }
};

template <class T> 
class AddLOC : public T
{
  public:
    T& linesOfCode(optional<string> prevloc, string loc, optional<string> nextloc) { 
      GetEI().ensureNixCode().ensureErrLine().prevLineOfCode = prevloc;
      GetEI().ensureNixCode().ensureErrLine().errLineOfCode = loc;
      GetEI().ensureNixCode().ensureErrLine().nextLineOfCode = nextloc;
      return *this;
    }
  protected:
    ErrorInfo& GetEI() { return T::GetEI(); }
};

typedef AddNixFile<AddErrLine<NixCode>> MkNixCode;

typedef AddName<AddDescription<EIError>> StandardError;
typedef AddName<AddDescription<EIWarning>> StandardWarning;

typedef AddName<
        AddDescription<
        AddNixFile<
        AddLineNumber<
        AddColumnRange<
        AddLOC<EIError>>>>>> MkNixError;
typedef AddName<
        AddDescription<
        AddNixFile<
        AddLineNumber<
        AddColumnRange<
        AddLOC<EIWarning>>>>>> MkNixWarning;

string showErrLine(ErrLine &errLine);

void print_code_lines(string &prefix, NixCode &nix_code); 

void print_error(ErrorInfo &einfo);
}

