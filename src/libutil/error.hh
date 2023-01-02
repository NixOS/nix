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

// the lines of code surrounding an error.
struct LinesOfCode {
    std::optional<std::string> prevLineOfCode;
    std::optional<std::string> errLineOfCode;
    std::optional<std::string> nextLineOfCode;
};

/* An abstract type that represents a location in a source file. */
struct AbstractPos
{
    uint32_t line = 0;
    uint32_t column = 0;

    /* Return the contents of the source file. */
    virtual std::optional<std::string> getSource() const
    { return std::nullopt; };

    virtual void print(std::ostream & out) const = 0;

    std::optional<LinesOfCode> getCodeLines() const;
};

std::ostream & operator << (std::ostream & str, const AbstractPos & pos);

void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const AbstractPos & errPos,
    const LinesOfCode & loc);

struct Trace {
    std::shared_ptr<AbstractPos> pos;
    hintformat hint;
    bool frame;
};

struct ErrorInfo {
    Verbosity level;
    hintformat msg;
    std::shared_ptr<AbstractPos> errPos;
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

    BaseError(const BaseError &) = default;

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

    void pushTrace(Trace trace)
    {
        err.traces.push_front(trace);
    }

    template<typename... Args>
    void addTrace(std::shared_ptr<AbstractPos> && e, std::string_view fs, const Args & ... args)
    {
        addTrace(std::move(e), hintfmt(std::string(fs), args...));
    }

    void addTrace(std::shared_ptr<AbstractPos> && e, hintformat hint, bool frame = false);

    bool hasTrace() const { return !err.traces.empty(); }

    const ErrorInfo & info() { return err; };
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
    SysError(int errNo_, const Args & ... args)
        : Error("")
    {
        errNo = errNo_;
        auto hf = hintfmt(args...);
        err.msg = hintfmt("%1%: %2%", normaltxt(hf.str()), strerror(errNo));
    }

    template<typename... Args>
    SysError(const Args & ... args)
        : SysError(errno, args ...)
    {
    }
};

}
