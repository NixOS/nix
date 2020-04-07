#ifndef error_hh
#define error_hh

#include "ansicolor.hh"
#include <string>
#include <optional>
#include <iostream>
#include <iomanip>

#include "types.hh"

namespace nix
{

typedef enum {
    elWarning,
    elError
} ErrLevel;

class ColumnRange
{
public:
    unsigned int start;
    unsigned int len;
};

class ErrorInfo;

class ErrLine
{
public:
    int lineNumber;
    std::optional<ColumnRange> columnRange;
    std::optional<string> prevLineOfCode;
    string errLineOfCode;
    std::optional<string> nextLineOfCode;
};

class NixCode
{
public:
    std::optional<string> nixFile;
    std::optional<ErrLine> errLine;
};

// ----------------------------------------------------------------
// format for hints.  same as fmt, except templated values
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

class hintformat
{
public:
    hintformat(string format) :fmt(format)
    {
        fmt.exceptions(boost::io::all_error_bits ^ boost::io::too_many_args_bit);
    }
    template<class T>
    hintformat& operator%(const T &value)
    {
        fmt % yellowify(value);
        return *this;
    }

    std::string str() const
    {
        return fmt.str();
    }

    template <typename U>
    friend class AddHint;
private:
    format fmt;
};

template<typename... Args>
inline hintformat hintfmt(const std::string & fs, const Args & ... args)
{
    hintformat f(fs);
    formatHelper(f, args...);
    return f;
}

// -------------------------------------------------
// ErrorInfo.
class ErrorInfo
{
public:
    ErrLevel level;
    string name;
    string description;
    std::optional<NixCode> nixCode;
    std::optional<string> hint;

    static std::optional<string> programName;

    static ErrorInfo ProgramError(const string &name,
                                  const string &description,
                                  const std::optional<hintformat> &hf);


    static ErrorInfo ProgramWarning(const string &name,
                                    const string &description,
                                    const std::optional<hintformat> &hf);


    template <class P>
    static ErrorInfo NixLangError(const string &name,
                                  const string &description,
                                  const P &pos,
                                  std::optional<string> prevloc,
                                  string loc,
                                  std::optional<string> nextloc,
                                  const std::optional<hintformat> &hf)
    {
        return NixLangEI(elError, name, description, pos, prevloc, loc, nextloc, hf);
    }


    template <class P>
    static ErrorInfo NixLangWarning(const string &name,
                                    const string &description,
                                    const P &pos,
                                    std::optional<string> prevloc,
                                    string loc,
                                    std::optional<string> nextloc,
                                    const std::optional<hintformat> &hf)
    {
        return NixLangEI(elWarning, name, description, pos, prevloc, loc, nextloc, hf);
    }



private:
    template <class P>
    static ErrorInfo NixLangEI(ErrLevel level,
                               const string &name,
                               const string &description,
                               const P &pos,
                               std::optional<string> prevloc,
                               string loc,
                               std::optional<string> nextloc,
                               const std::optional<hintformat> &hf)
    {
        ErrorInfo ei(level);
        ei.name = name;
        ei.description = description;
        if (hf.has_value())
            ei.hint = std::optional<string>(hf->str());
        else
            ei.hint = std::nullopt;

        ErrLine errline;
        errline.lineNumber = pos.line;
        errline.columnRange = { .start = pos.column, .len = 1 };
        errline.prevLineOfCode = prevloc;
        errline.errLineOfCode = loc;
        errline.nextLineOfCode = nextloc;
        NixCode nixcode;
        nixcode.nixFile = pos.file;
        nixcode.errLine = std::optional(errline);
        ei.nixCode = std::optional(nixcode);

        return ei;
    }

    static ErrorInfo ProgramEI(ErrLevel level,
                               const string &name,
                               const string &description,
                               const std::optional<hintformat> &hf);



    // constructor is protected, so only the builder classes can create an ErrorInfo.
    ErrorInfo(ErrLevel level)
    {
        this->level = level;
    }
};

/*
template <class T>
class AddPos : private T
{
public:
    template <class P>
    T& pos(const P &aPos)
    {
        GetEI().ensureNixCode().nixFile = aPos.file;
        GetEI().ensureNixCode().ensureErrLine().lineNumber = aPos.line;
        GetEI().ensureNixCode().ensureErrLine().columnRange = { .start = aPos.column, .len = 1 };
        return *this;
    }
protected:
    ErrorInfo& GetEI()
    {
        return T::GetEI();
    }
};

template <class T>
class AddLOC : private T
{
public:
    T& linesOfCode(std::optional<string> prevloc, string loc, std::optional<string> nextloc)
    {
        GetEI().ensureNixCode().ensureErrLine().prevLineOfCode = prevloc;
        GetEI().ensureNixCode().ensureErrLine().errLineOfCode = loc;
        GetEI().ensureNixCode().ensureErrLine().nextLineOfCode = nextloc;
        return *this;
    }
protected:
    ErrorInfo& GetEI()
    {
        return T::GetEI();
    }
};
*/

/*
// the template layer for adding a hint.
template <class T>
class AddHint : private T
{
public:
    T& hint(const hintformat &hf)
    {
        GetEI().hint = std::optional(hf.str());
        return *this;
    }
    T& nohint()
    {
        GetEI().hint = std::nullopt;
        return *this;
    }
protected:
    ErrorInfo& GetEI()
    {
        return T::GetEI();
    }
};
*/

// --------------------------------------------------------
// error types

/*typedef AddName<
        AddDescription<
        AddHint<
        EIError>>> ProgramError;

typedef AddName<
        AddDescription<
        AddHint<
        EIWarning>>> ProgramWarning;

typedef AddName<
        AddDescription<
        AddPos<
        AddLOC<
        AddHint<
        EIError>>>>> NixLangError;

typedef AddName<
        AddDescription<
        AddPos<
        AddLOC<
        AddHint<
        EIWarning>>>>> NixLangWarning;


*/
// --------------------------------------------------------
// error printing

// just to cout for now.
void printErrorInfo(const ErrorInfo &einfo);

}

#endif
