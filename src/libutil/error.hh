#ifndef error_hh
#define error_hh

#include "ansicolor.hh"
#include <string>
#include <optional>
#include <iostream>
#include <iomanip>

#include <boost/format.hpp>

namespace nix {

typedef enum { 
    elWarning
    , elError
    } ErrLevel;

class ColumnRange { 
    public:
        unsigned int start;
        unsigned int len;
};

class ErrorInfo;

class ErrLine { 
    public:
        int lineNumber;
        std::optional<ColumnRange> columnRange;
        std::optional<string> prevLineOfCode;
        string errLineOfCode;
        std::optional<string> nextLineOfCode;
};

class NixCode { 
    public:
        std::optional<string> nixFile;
        std::optional<ErrLine> errLine;

        ErrLine& ensureErrLine() 
        { 
            if (!this->errLine.has_value())
                 this->errLine = std::optional(ErrLine());
            return *this->errLine; 
        }
};

// -------------------------------------------------
// ErrorInfo.

// Forward friend class declarations.    "builder classes"
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
        std::optional<NixCode> nixCode;
        std::optional<string> hint;
        ErrorInfo& GetEI() { return *this; }

        static std::optional<string> programName;

        // give these access to the private constructor, 
        // when they are direct descendants (children but not grandchildren).
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
                 this->nixCode = std::optional(NixCode());
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
class AddNixFile : private T
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
class AddLineNumber : private T
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
class AddColumnRange : private T
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
class AddLOC : private T
{
    public:
        T& linesOfCode(std::optional<string> prevloc, string loc, std::optional<string> nextloc) { 
            GetEI().ensureNixCode().ensureErrLine().prevLineOfCode = prevloc;
            GetEI().ensureNixCode().ensureErrLine().errLineOfCode = loc;
            GetEI().ensureNixCode().ensureErrLine().nextLineOfCode = nextloc;
            return *this;
        }
    protected:
        ErrorInfo& GetEI() { return T::GetEI(); }
};


// ----------------------------------------------------------------
// format for hints.  same as boost format, except templated values 
// are always in yellow.

template <class T>
class yellowify
{
public:
        yellowify(T &s) : value(s) {}
        T &value;
};

template <class T>
std::ostream& operator<<(std::ostream &out, const yellowify<T> &y)
{
    return out << ANSI_YELLOW << y.value << ANSI_NORMAL;
}

class hintfmt 
{
    public:
        hintfmt(string format) :fmt(format) {
            fmt.exceptions(boost::io::all_error_bits ^ boost::io::too_many_args_bit);
          }
        template<class T>
            hintfmt& operator%(const T &value) { fmt % yellowify(value); return *this; }

        template <typename U>
        friend class AddHint;
    private:
        format fmt;

};

// the template layer for adding a hint.
template <class T> 
class AddHint : private T
{
    public:
        T& hint(hintfmt &hfmt) {
            GetEI().hint = std::optional(hfmt.fmt.str());
            return *this;
        }
        T& nohint() {
            GetEI().hint = std::nullopt;
            return *this;
        }
    protected:
        ErrorInfo& GetEI() { return T::GetEI(); }
};

// --------------------------------------------------------
// error types

typedef AddName<
        AddDescription<
        AddHint<
        EIError>>> ProgramError;

typedef AddName<
        AddDescription<
        AddHint<
        EIWarning>>> ProgramWarning;

typedef AddName<
        AddDescription<
        AddNixFile<
        AddLineNumber<
        AddColumnRange<
        AddLOC<
        AddHint<
        EIError>>>>>>> NixLangError;

typedef AddName<
        AddDescription<
        AddNixFile<
        AddLineNumber<
        AddColumnRange<
        AddLOC<
        AddHint<
        EIWarning>>>>>>> NixLangWarning;


// --------------------------------------------------------
// error printing

// just to cout for now.
void printErrorInfo(ErrorInfo &einfo);

}

#endif
