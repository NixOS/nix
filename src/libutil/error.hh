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

class ErrPos
{
public:
    int lineNumber;
    int column;
    string nixFile;

    template <class P>
    ErrPos& operator=(const P &pos)
    {
        lineNumber = pos.line;
        column = pos.column;
        nixFile = pos.file.str();
        return *this;
    }
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

std::ostream& operator<<(std::ostream &os, const hintformat &hf)
{
    return os << hf.str();
}

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
    std::optional<hintformat> hint;
    std::optional<string> prevLineOfCode;
    string errLineOfCode;
    std::optional<string> nextLineOfCode;
    std::optional<ErrPos> errPos;

    static std::optional<string> programName;

private:
    // template <class P>
    // static ErrorInfo NixLangEI(ErrLevel level,
    //                            const string &name,
    //                            const string &description,
    //                            const P &pos,
    //                            std::optional<string> prevloc,
    //                            string loc,
    //                            std::optional<string> nextloc,
    //                            const std::optional<hintformat> &hf)
    // {
    //     ErrorInfo ei(level);
    //     ei.name = name;
    //     ei.description = description;
    //     if (hf.has_value())
    //         ei.hint = std::optional<string>(hf->str());
    //     else
    //         ei.hint = std::nullopt;

    //     ErrLine errline;
    //     errline.lineNumber = pos.line;
    //     errline.column = pos.column;
    //     errline.prevLineOfCode = prevloc;
    //     errline.errLineOfCode = loc;
    //     errline.nextLineOfCode = nextloc;
    //     NixCode nixcode;
    //     nixcode.nixFile = pos.file;
    //     nixcode.errLine = std::optional(errline);
    //     ei.nixCode = std::optional(nixcode);

    //     return ei;
    // }

    // static ErrorInfo ProgramEI(ErrLevel level,
    //                            const string &name,
    //                            const string &description,
    //                            const std::optional<hintformat> &hf);



    // constructor is protected, so only the builder classes can create an ErrorInfo.

};

// --------------------------------------------------------
// error printing

// just to cout for now.
void printErrorInfo(const ErrorInfo &einfo);

}

#endif
