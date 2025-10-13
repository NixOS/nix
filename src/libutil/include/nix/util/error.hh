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

#include "nix/util/suggestions.hh"
#include "nix/util/fmt.hh"

#include <cstring>
#include <list>
#include <memory>
#include <optional>
#include <utility>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

namespace nix {

typedef enum { lvlError = 0, lvlWarn, lvlNotice, lvlInfo, lvlTalkative, lvlChatty, lvlDebug, lvlVomit } Verbosity;

/**
 * The lines of code surrounding an error.
 */
struct LinesOfCode
{
    std::optional<std::string> prevLineOfCode;
    std::optional<std::string> errLineOfCode;
    std::optional<std::string> nextLineOfCode;
};

/* NOTE: position.hh recursively depends on source-path.hh -> source-accessor.hh
   -> hash.hh -> configuration.hh -> experimental-features.hh -> error.hh -> Pos.
   There are other such cycles.
   Thus, Pos has to be an incomplete type in this header. But since ErrorInfo/Trace
   have to refer to Pos, they have to use pointer indirection via std::shared_ptr
   to break the recursive header dependency.
   FIXME: Untangle this mess. Should there be AbstractPos as there used to be before
   4feb7d9f71? */
struct Pos;

void printCodeLines(std::ostream & out, const std::string & prefix, const Pos & errPos, const LinesOfCode & loc);

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

struct Trace
{
    std::shared_ptr<const Pos> pos;
    HintFmt hint;
    TracePrint print = TracePrint::Default;
};

inline std::strong_ordering operator<=>(const Trace & lhs, const Trace & rhs);

struct ErrorInfo
{
    Verbosity level;
    HintFmt msg;
    std::shared_ptr<const Pos> pos;
    std::list<Trace> traces;
    /**
     * Some messages are generated directly by expressions; notably `builtins.warn`, `abort`, `throw`.
     * These may be rendered differently, so that users can distinguish them.
     */
    bool isFromExpr = false;

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
    BaseError & operator=(const BaseError &) = default;
    BaseError & operator=(BaseError &&) = default;

    template<typename... Args>
    BaseError(unsigned int status, const Args &... args)
        : err{.level = lvlError, .msg = HintFmt(args...), .status = status}
    {
    }

    template<typename... Args>
    explicit BaseError(const std::string & fs, const Args &... args)
        : err{.level = lvlError, .msg = HintFmt(fs, args...)}
    {
    }

    template<typename... Args>
    BaseError(const Suggestions & sug, const Args &... args)
        : err{.level = lvlError, .msg = HintFmt(args...), .suggestions = sug}
    {
    }

    BaseError(HintFmt hint)
        : err{.level = lvlError, .msg = hint}
    {
    }

    BaseError(ErrorInfo && e)
        : err(std::move(e))
    {
    }

    BaseError(const ErrorInfo & e)
        : err(e)
    {
    }

    /** The error message without "error: " prefixed to it. */
    std::string message()
    {
        return err.msg.str();
    }

    const char * what() const noexcept override
    {
        return calcWhat().c_str();
    }

    const std::string & msg() const
    {
        return calcWhat();
    }

    const ErrorInfo & info() const
    {
        calcWhat();
        return err;
    }

    void withExitStatus(unsigned int status)
    {
        err.status = status;
    }

    void atPos(std::shared_ptr<const Pos> pos)
    {
        err.pos = pos;
    }

    void pushTrace(Trace trace)
    {
        err.traces.push_front(trace);
    }

    /**
     * Prepends an item to the error trace, as is usual for extra context.
     *
     * @param pos Nullable source position to put in trace item
     * @param fs Format string, see `HintFmt`
     * @param args... Format string arguments.
     */
    template<typename... Args>
    void addTrace(std::shared_ptr<const Pos> && pos, std::string_view fs, const Args &... args)
    {
        addTrace(std::move(pos), HintFmt(std::string(fs), args...));
    }

    /**
     * Prepends an item to the error trace, as is usual for extra context.
     *
     * @param pos Nullable source position to put in trace item
     * @param hint Formatted error message
     * @param print Optional, whether to always print (used by `addErrorContext`)
     */
    void addTrace(std::shared_ptr<const Pos> && pos, HintFmt hint, TracePrint print = TracePrint::Default);

    bool hasTrace() const
    {
        return !err.traces.empty();
    }

    const ErrorInfo & info()
    {
        return err;
    };
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass  \
    {                                   \
    public:                             \
        using superClass::superClass;   \
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
 * and `windows::WinError` without breaking catch blocks.
 *
 * However, it is permissible to catch this and rethrow so long as
 * certain conditions are not met (e.g. to catch only if `errNo =
 * EFooBar`). In that case, try to also catch the equivalent `windows::WinError`
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
    SysError(int errNo, const Args &... args)
        : SystemError("")
        , errNo(errNo)
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
    SysError(const Args &... args)
        : SysError(errno, args...)
    {
    }
};

#ifdef _WIN32
namespace windows {
class WinError;
}
#endif

/**
 * Convenience alias for when we use a `errno`-based error handling
 * function on Unix, and `GetLastError()`-based error handling on on
 * Windows.
 */
using NativeSysError =
#ifdef _WIN32
    windows::WinError
#else
    SysError
#endif
    ;

/**
 * Throw an exception for the purpose of checking that exception
 * handling works; see 'initLibUtil()'.
 */
void throwExceptionSelfCheck();

/**
 * Print a message and std::terminate().
 */
[[noreturn]]
void panic(std::string_view msg);

/**
 * Print a basic error message with source position and std::terminate().
 *
 * @note: This assumes that the logger is operational
 */
[[gnu::noinline, gnu::cold, noreturn]] void unreachable(std::source_location loc = std::source_location::current());

} // namespace nix
