#pragma once
/**
 * @file
 *
 * @brief This file defines two main structs/classes used in nix error handling.
 *
 * ErrorInfo provides a standard payload of error information, with conversion to string
 * happening in the logger rather than at the call site.
 *
 * BaseError is the ancestor of nix specific exceptions (and Interrupted), and contains
 * an ErrorInfo.
 *
 * ErrorInfo structs are sent to the logger as part of an exception, or directly with the
 * logError or logWarning macros.
 * See libutil/tests/logging.cc for usage examples.
 */

#include "suggestions.hh"
#include "ref.hh"
#include "types.hh"
#include "fmt.hh"

#include <cstring>
#include <list>
#include <memory>
#include <map>
#include <optional>
#include <compare>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {


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

/**
 * The lines of code surrounding an error.
 */
struct LinesOfCode {
    std::optional<std::string> prevLineOfCode;
    std::optional<std::string> errLineOfCode;
    std::optional<std::string> nextLineOfCode;
};

struct Pos;

void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const Pos & errPos,
    const LinesOfCode & loc);

/**
 * When a stack frame is printed.
 */
enum struct TracePrint {
    /**
     * The default behavior; always printed when `--show-trace` is set.
     */
    Default,
    /** Always printed. Produced by `builtins.addErrorContext`. */
    Always,
};

struct Trace {
    std::shared_ptr<Pos> pos;
    HintFmt hint;
    TracePrint print = TracePrint::Default;
};

inline bool operator<(const Trace& lhs, const Trace& rhs);
inline bool operator> (const Trace& lhs, const Trace& rhs);
inline bool operator<=(const Trace& lhs, const Trace& rhs);
inline bool operator>=(const Trace& lhs, const Trace& rhs);

struct ErrorInfo {
    Verbosity level;
    HintFmt msg;
    std::shared_ptr<Pos> pos;
    std::list<Trace> traces;

    /**
     * Exit status.
     */
    unsigned int status = 1;

    Suggestions suggestions;

    static std::optional<std::string> programName;
};

std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, bool showTrace);

/**
 * BaseError should generally not be caught, as it has Interrupted as
 * a subclass. Catch Error instead.
 */
class BaseError : public std::exception
{
protected:
    mutable ErrorInfo err;

    /**
     * Cached formatted contents of `err.msg`.
     */
    mutable std::optional<std::string> what_;
    /**
     * Format `err.msg` and set `what_` to the resulting value.
     */
    const std::string & calcWhat() const;

public:
    BaseError(const BaseError &) = default;

    template<typename... Args>
    BaseError(unsigned int status, const Args & ... args)
        : err { .level = lvlError, .msg = HintFmt(args...), .status = status }
    { }

    template<typename... Args>
    explicit BaseError(const std::string & fs, const Args & ... args)
        : err { .level = lvlError, .msg = HintFmt(fs, args...) }
    { }

    template<typename... Args>
    BaseError(const Suggestions & sug, const Args & ... args)
        : err { .level = lvlError, .msg = HintFmt(args...), .suggestions = sug }
    { }

    BaseError(HintFmt hint)
        : err { .level = lvlError, .msg = hint }
    { }

    BaseError(ErrorInfo && e)
        : err(std::move(e))
    { }

    BaseError(const ErrorInfo & e)
        : err(e)
    { }

    std::string message() {
        return err.msg.str();
    }

    const char * what() const noexcept override { return calcWhat().c_str(); }
    const std::string & msg() const { return calcWhat(); }
    const ErrorInfo & info() const { calcWhat(); return err; }

    void withExitStatus(unsigned int status)
    {
        err.status = status;
    }

    void atPos(std::shared_ptr<Pos> pos) {
        err.pos = pos;
    }

    void pushTrace(Trace trace)
    {
        err.traces.push_front(trace);
    }

    template<typename... Args>
    void addTrace(std::shared_ptr<Pos> && e, std::string_view fs, const Args & ... args)
    {
        addTrace(std::move(e), HintFmt(std::string(fs), args...));
    }

    void addTrace(std::shared_ptr<Pos> && e, HintFmt hint, TracePrint print = TracePrint::Default);

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

/**
 * To use in catch-blocks.
 */
MakeError(SystemError, Error);

/**
 * POSIX system error, created using `errno`, `strerror` friends.
 *
 * Throw this, but prefer not to catch this, and catch `SystemError`
 * instead. This allows implementations to freely switch between this
 * and `WinError` without breaking catch blocks.
 *
 * However, it is permissible to catch this and rethrow so long as
 * certain conditions are not met (e.g. to catch only if `errNo =
 * EFooBar`). In that case, try to also catch the equivalent `WinError`
 * code.
 *
 * @todo Rename this to `PosixError` or similar. At this point Windows
 * support is too WIP to justify the code churn, but if it is finished
 * then a better identifier becomes moe worth it.
 */
class SysError : public SystemError
{
public:
    int errNo;

    /**
     * Construct using the explicitly-provided error number. `strerror`
     * will be used to try to add additional information to the message.
     */
    template<typename... Args>
    SysError(int errNo, const Args & ... args)
        : SystemError(""), errNo(errNo)
    {
        auto hf = HintFmt(args...);
        err.msg = HintFmt("%1%: %2%", Uncolored(hf.str()), strerror(errNo));
    }

    /**
     * Construct using the ambient `errno`.
     *
     * Be sure to not perform another `errno`-modifying operation before
     * calling this constructor!
     */
    template<typename... Args>
    SysError(const Args & ... args)
        : SysError(errno, args ...)
    {
    }
};

/**
 * Throw an exception for the purpose of checking that exception
 * handling works; see 'initLibUtil()'.
 */
void throwExceptionSelfCheck();

}
