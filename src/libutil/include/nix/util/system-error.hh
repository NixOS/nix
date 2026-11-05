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
 * Shared, reusable implementation for a `SystemError` for a particular kind of
 * native error code.
 *
 * This is used for `SysError` (for `errno`) and `windows::WinError` (for
 * `GetLastError()`).
 *
 * `Derived` must provide these static methods:
 *
 * - `ambientError()`, the ambient code, which the constructors not taking one
 *   use.
 *
 * - `toErrorCode(code)`, the `std::error_code` that a `Derived` for `code` has.
 *
 * - `renderError(code)`, the description of `code` that a `Derived` for it
 *   appends to its message.
 *
 * Together, `ambientError` and `toErrorCode` are for checking what went wrong,
 * e.g. by comparing with `std::errc`, where that is an expected outcome rather
 * than an exception, without constructing a `Derived` just to call
 * `SystemError::is`. If it does need throwing after all, pass the code to the
 * constructor taking one.
 *
 * The native code itself is stored in `Fields::*field`, e.g. `SysError::errNo`
 * or `WinError::lastError`. Each kind of code keeps its own name, so that code
 * using one where the other is meant, e.g. comparing a `WinError` code with an
 * `errno` value, does not mistakenly compile because of bad implicit
 * conversions.
 *
 * The field is in a separate base, `Fields`, rather than in `Derived` itself,
 * since `Derived`'s own members are not yet constructed when this class's
 * constructors set it.
 */
template<typename Derived, typename Fields, auto Fields::* field>
class NativeCodeSystemError : public Fields, public CloneableError<Derived, SystemError>
{
    using Base = CloneableError<Derived, SystemError>;

public:
    using NativeCode = std::remove_cvref_t<decltype(std::declval<Fields &>().*field)>;

    /**
     * Construct using the explicitly-provided error code, which
     * `Derived::renderError` describes in the message.
     */
    template<typename... Args>
    NativeCodeSystemError(NativeCode code, Args &&... args)
        : Fields{code}
        , Base(
              typename Base::DisambigVarArgs{},
              Derived::toErrorCode(code),
              Derived::renderError(code),
              std::forward<Args>(args)...)
    {
    }

    /**
     * Construct using the explicitly-provided error code, which
     * `Derived::renderError` describes in the message.
     *
     * Unlike above, the `HintFmt` already exists rather than being made on
     * the spot.
     */
    NativeCodeSystemError(NativeCode code, const HintFmt & hf)
        : Fields{code}
        , Base(typename Base::DisambigHintFmt{}, Derived::toErrorCode(code), Derived::renderError(code), hf)
    {
    }

    /**
     * Construct using the ambient error code.
     *
     * Be sure to not perform another operation that may change it before
     * calling this constructor!
     */
    template<typename... Args>
    NativeCodeSystemError(Args &&... args)
        : NativeCodeSystemError(Derived::ambientError(), std::forward<Args>(args)...)
    {
    }

    /**
     * Construct using the ambient error code and a function that
     * produces a `HintFmt`. The error code is read first, then the
     * function is called, so the function is safe to change it.
     */
    NativeCodeSystemError(auto && mkHintFmt)
        requires std::invocable<decltype(mkHintFmt)> && std::same_as<std::invoke_result_t<decltype(mkHintFmt)>, HintFmt>
        : NativeCodeSystemError(captureAmbientError(std::forward<decltype(mkHintFmt)>(mkHintFmt)))
    {
    }

private:
    /**
     * Helper to ensure the error code is captured before mkHintFmt is
     * called. C++ argument evaluation order is unspecified, so we can't
     * rely on `NativeCodeSystemError(Derived::ambientError(), mkHintFmt())`
     * evaluating `Derived::ambientError()` first.
     */
    static std::pair<NativeCode, HintFmt> captureAmbientError(auto && mkHintFmt)
    {
        NativeCode e = Derived::ambientError();
        return {e, mkHintFmt()};
    }

    NativeCodeSystemError(std::pair<NativeCode, HintFmt> && p)
        : NativeCodeSystemError(p.first, std::move(p.second))
    {
    }
};

/**
 * The fields of `SysError` that `NativeCodeSystemError` sets.
 */
struct SysErrorFields
{
    int errNo;
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
class SysError final : public NativeCodeSystemError<SysError, SysErrorFields, &SysErrorFields::errNo>
{
    void anchor() override;

public:
    using NativeCodeSystemError::NativeCodeSystemError;

    /**
     * The ambient `errno`. See `NativeCodeSystemError`.
     */
    static NativeCode ambientError()
    {
        return errno;
    }

    /**
     * See `NativeCodeSystemError`.
     */
    static std::error_code toErrorCode(NativeCode code)
    {
        return std::make_error_code(static_cast<std::errc>(code));
    }

    /**
     * With `strerror`. See `NativeCodeSystemError`.
     */
    static std::string renderError(NativeCode code)
    {
        return strerror(code);
    }
};

#ifdef _WIN32

namespace windows {

/**
 * The fields of `WinError` that `NativeCodeSystemError` sets.
 */
struct WinErrorFields
{
    DWORD lastError;
};

/**
 * Windows Error type.
 *
 * Unless you need to catch a specific error number, don't catch this in
 * portable code. Catch `SystemError` instead.
 */
class WinError : public NativeCodeSystemError<WinError, WinErrorFields, &WinErrorFields::lastError>
{
    void anchor() override;

public:
    using NativeCodeSystemError::NativeCodeSystemError;

    /**
     * The ambient `GetLastError()`. See `NativeCodeSystemError`.
     */
    static NativeCode ambientError()
    {
        return GetLastError();
    }

    /**
     * See `NativeCodeSystemError`.
     */
    static std::error_code toErrorCode(NativeCode code)
    {
        return std::error_code(code, std::system_category());
    }

    /**
     * With `FormatMessageA`. See `NativeCodeSystemError`.
     */
    static std::string renderError(NativeCode code);
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
