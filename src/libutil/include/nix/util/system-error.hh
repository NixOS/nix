#pragma once
/**
 * @file
 *
 * @brief Errors from the operating system: `SystemError`, and its
 * subclasses for the native error codes, `SysError` for `errno` and
 * `windows::WinError` for `GetLastError()`, and `NativeSysError` for
 * whichever of those is native.
 */

#include "nix/util/error.hh"

#include <cerrno>
#include <concepts>
#include <cstring>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

#ifdef _WIN32
#  include <errhandlingapi.h>
#endif

namespace nix {

/**
 * To use in catch-blocks. Provides a convenience method to get the portable
 * std::error_code. Use when you want to catch and check an error condition like
 * no_such_file_or_directory (ENOENT) without ifdefs.
 */
class SystemError : public CloneableError<SystemError, Error>
{
    std::error_code errorCode;
    std::string errorDetails;

    void anchor() override;

protected:

    /**
     * Just here to allow derived classes to use the right constructor
     * (the protected one).
     *
     * This one indicates the prebuilt `HintFmt` one with the explicit `errorDetails`
     */
    struct DisambigHintFmt
    {};

    /**
     * Just here to allow derived classes to use the right constructor
     * (the protected one).
     *
     * This one indicates the varargs one to build the `HintFmt` with the explicit `errorDetails`
     */
    struct DisambigVarArgs
    {};

    /**
     * Protected constructor that takes a pre-built HintFmt.
     * Use this when the error message needs to be constructed before
     * capturing errno/GetLastError().
     */
    SystemError(DisambigHintFmt, std::error_code errorCode, std::string_view errorDetails, const HintFmt & hf)
        : CloneableError(HintFmt{"%s: %s", Uncolored(hf.str()), errorDetails})
        , errorCode(errorCode)
        , errorDetails(errorDetails)
    {
    }

    /**
     * Protected constructor for subclasses that provide their own error message.
     * The error message is appended to the formatted hint.
     */
    template<typename... Args>
    SystemError(DisambigVarArgs, std::error_code errorCode, std::string_view errorDetails, Args &&... args)
        : SystemError(DisambigHintFmt{}, errorCode, errorDetails, HintFmt{std::forward<Args>(args)...})
    {
    }

public:
    /**
     * Construct with an error code. The error code's message is automatically
     * appended to the error message.
     */
    SystemError(std::error_code errorCode, const HintFmt & hf)
        : SystemError(DisambigHintFmt{}, errorCode, errorCode.message(), hf)
    {
    }

    /**
     * Construct with an error code. The error code's message is automatically
     * appended to the error message.
     */
    template<typename... Args>
    SystemError(std::error_code errorCode, Args &&... args)
        : SystemError(DisambigVarArgs{}, errorCode, errorCode.message(), std::forward<Args>(args)...)
    {
    }

    const std::error_code ec() const &
    {
        return errorCode;
    }

    bool is(std::errc e) const
    {
        return errorCode == e;
    }

    template<std::same_as<std::errc>... Rest>
    bool is(std::errc e, Rest... rest) const
    {
        return is(e) || (is(rest) || ...);
    }
};

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
class SysError final : public CloneableError<SysError, SystemError>
{
    void anchor() override;

public:
    int errNo;

    /**
     * Construct using the explicitly-provided error number. `strerror`
     * will be used to try to add additional information to the message.
     */
    template<typename... Args>
    SysError(int errNo, Args &&... args)
        : CloneableError(
              DisambigVarArgs{},
              std::make_error_code(static_cast<std::errc>(errNo)),
              strerror(errNo),
              std::forward<Args>(args)...)
        , errNo(errNo)
    {
    }

    /**
     * Construct using the explicitly-provided error number. `strerror`
     * will be used to try to add additional information to the message.
     *
     * Unlike above, the `HintFmt` already exists rather than being made on
     * the spot.
     */
    SysError(int errNo, const HintFmt & hf)
        : CloneableError(DisambigHintFmt{}, std::make_error_code(static_cast<std::errc>(errNo)), strerror(errNo), hf)
        , errNo(errNo)
    {
    }

    /**
     * Construct using the ambient `errno`.
     *
     * Be sure to not perform another `errno`-modifying operation before
     * calling this constructor!
     */
    template<typename... Args>
    SysError(Args &&... args)
        : SysError(errno, std::forward<Args>(args)...)
    {
    }

    /**
     * Construct using the ambient `errno` and a function that produces
     * a `HintFmt`. errno is read first, then the function is called, so
     * the function is safe to modify `errno`.
     */
    SysError(auto && mkHintFmt)
        requires std::invocable<decltype(mkHintFmt)> && std::same_as<std::invoke_result_t<decltype(mkHintFmt)>, HintFmt>
        : SysError(captureErrno(std::forward<decltype(mkHintFmt)>(mkHintFmt)))
    {
    }

private:
    /**
     * Helper to ensure errno is captured before mkHintFmt is called.
     * C++ argument evaluation order is unspecified, so we can't rely on
     * `SysError(errno, mkHintFmt())` evaluating errno first.
     */
    static std::pair<int, HintFmt> captureErrno(auto && mkHintFmt)
    {
        int e = errno;
        return {e, mkHintFmt()};
    }

    SysError(std::pair<int, HintFmt> && p)
        : SysError(p.first, std::move(p.second))
    {
    }
};

#ifdef _WIN32

namespace windows {

/**
 * Windows Error type.
 *
 * Unless you need to catch a specific error number, don't catch this in
 * portable code. Catch `SystemError` instead.
 */
class WinError : public CloneableError<WinError, SystemError>
{
    void anchor() override;

public:
    DWORD lastError;

    /**
     * Construct using the explicitly-provided error number.
     * `FormatMessageA` will be used to try to add additional
     * information to the message.
     */
    template<typename... Args>
    WinError(DWORD lastError, Args &&... args)
        : CloneableError(
              DisambigVarArgs{},
              std::error_code(lastError, std::system_category()),
              renderError(lastError),
              std::forward<Args>(args)...)
        , lastError(lastError)
    {
    }

    /**
     * Construct using the explicitly-provided error number.
     * `FormatMessageA` will be used to try to add additional
     * information to the message.
     *
     * Unlike above, the `HintFmt` already exists rather than being made on
     * the spot.
     */
    WinError(DWORD lastError, const HintFmt & hf)
        : CloneableError(
              DisambigHintFmt{}, std::error_code(lastError, std::system_category()), renderError(lastError), hf)
        , lastError(lastError)
    {
    }

    /**
     * Construct using `GetLastError()` and the ambient "last error".
     *
     * Be sure to not perform another last-error-modifying operation
     * before calling this constructor!
     */
    template<typename... Args>
    WinError(Args &&... args)
        : WinError(GetLastError(), std::forward<Args>(args)...)
    {
    }

    /**
     * Construct using `GetLastError()` and a function that produces a
     * `HintFmt`. `GetLastError()` is called first, then the function is
     * called, so the function is safe to modify the last error.
     */
    WinError(auto && mkHintFmt)
        requires std::invocable<decltype(mkHintFmt)> && std::same_as<std::invoke_result_t<decltype(mkHintFmt)>, HintFmt>
        : WinError(captureLastError(std::forward<decltype(mkHintFmt)>(mkHintFmt)))
    {
    }

private:
    /**
     * Helper to ensure GetLastError() is captured before mkHintFmt is called.
     * C++ argument evaluation order is unspecified, so we can't rely on
     * `WinError(GetLastError(), mkHintFmt())` evaluating GetLastError() first.
     */
    static std::pair<DWORD, HintFmt> captureLastError(auto && mkHintFmt)
    {
        DWORD e = GetLastError();
        return {e, mkHintFmt()};
    }

    WinError(std::pair<DWORD, HintFmt> && p)
        : WinError(p.first, std::move(p.second))
    {
    }

    static std::string renderError(DWORD lastError);
};

} // namespace windows

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

} // namespace nix
