#pragma once

#include "suggestions.hh"
#include "ref.hh"
#include "types.hh"
#include "fmt.hh"

#include <cstring>
#include <list>
#include <memory>
#include <map>
#include <optional>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Before 4.7, gcc's std::exception uses empty throw() specifiers for
 * its (virtual) destructor and what() in c++11 mode, in violation of spec
 */
#ifdef __GNUC__
#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
#define EXCEPTION_NEEDS_THROW_SPEC
#endif
#endif

namespace nix {

/*

   This file defines two main structs/classes used in nix error handling.

   ErrorInfo provides a standard payload of error information, with conversion to string
   happening in the logger rather than at the call site.

   BaseError is the ancestor of nix specific exceptions (and Interrupted), and contains
   an ErrorInfo.

   ErrorInfo structs are sent to the logger as part of an exception, or directly with the
   logError or logWarning macros.

   See libutil/tests/logging.cc for usage examples.

 */

typedef enum {
    lvlError = 0,
    lvlWarn,
    lvlNotice,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

/* adjust Pos::origin bit width when adding stuff here */
typedef enum {
    foFile,
    foStdin,
    foString
} FileOrigin;

// the lines of code surrounding an error.
struct LinesOfCode {
    std::optional<std::string> prevLineOfCode;
    std::optional<std::string> errLineOfCode;
    std::optional<std::string> nextLineOfCode;
};

// ErrPos indicates the location of an error in a nix file.
struct ErrPos {
    int line = 0;
    int column = 0;
    std::string file;
    FileOrigin origin;

    operator bool() const
    {
        return line != 0;
    }

    // convert from the Pos struct, found in libexpr.
    template <class P>
    ErrPos & operator=(const P & pos)
    {
        origin = pos.origin;
        line = pos.line;
        column = pos.column;
        // is file symbol null?
        if (pos.file.set())
            file = pos.file;
        else
            file = "";
        return *this;
    }

    template <class P>
    ErrPos(const P & p)
    {
        *this = p;
    }
};

struct Trace {
    std::optional<ErrPos> pos;
    hintformat hint;
};

struct ErrorInfo {
    Verbosity level;
    hintformat msg;
    std::optional<ErrPos> errPos;
    std::list<Trace> traces;

    Suggestions suggestions;

    static std::optional<std::string> programName;
};

std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, bool showTrace);

/* BaseError should generally not be caught, as it has Interrupted as
   a subclass. Catch Error instead. */
class BaseError : public std::exception
{
protected:
    mutable ErrorInfo err;

    mutable std::optional<std::string> what_;
    const std::string & calcWhat() const;

public:
    unsigned int status = 1; // exit status

    template<typename... Args>
    BaseError(unsigned int status, const Args & ... args)
        : err { .level = lvlError, .msg = hintfmt(args...) }
        , status(status)
    { }

    template<typename... Args>
    explicit BaseError(const std::string & fs, const Args & ... args)
        : err { .level = lvlError, .msg = hintfmt(fs, args...) }
    { }

    template<typename... Args>
    BaseError(const Suggestions & sug, const Args & ... args)
        : err { .level = lvlError, .msg = hintfmt(args...), .suggestions = sug }
    { }

    BaseError(hintformat hint)
        : err { .level = lvlError, .msg = hint }
    { }

    BaseError(ErrorInfo && e)
        : err(std::move(e))
    { }

    BaseError(const ErrorInfo & e)
        : err(e)
    { }

#ifdef EXCEPTION_NEEDS_THROW_SPEC
    ~BaseError() throw () { };
    const char * what() const throw () { return calcWhat().c_str(); }
#else
    const char * what() const noexcept override { return calcWhat().c_str(); }
#endif

    const std::string & msg() const { return calcWhat(); }
    const ErrorInfo & info() const { calcWhat(); return err; }

    template<typename... Args>
    void addTrace(std::optional<ErrPos> e, const std::string & fs, const Args & ... args)
    {
        addTrace(e, hintfmt(fs, args...));
    }

    void addTrace(std::optional<ErrPos> e, hintformat hint);

    bool hasTrace() const { return !err.traces.empty(); }
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        using superClass::superClass;                   \
    }

MakeError(Error, BaseError);
MakeError(UsageError, Error);
MakeError(UnimplementedError, Error);

class SysError : public Error
{
public:
    int errNo;

    template<typename... Args>
    SysError(const Args & ... args)
        : Error("")
    {
        errNo = errno;
        auto hf = hintfmt(args...);
        err.msg = hintfmt("%1%: %2%", normaltxt(hf.str()), strerror(errNo));
    }
};

}
