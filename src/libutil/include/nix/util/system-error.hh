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
    SystemError(
        DisambigVarArgs,
        std::error_code errorCode,
        std::string_view errorDetails,
        FormatString fs,
        const Args &... args)
        : SystemError(DisambigHintFmt{}, errorCode, errorDetails, HintFmt(fs, args...))
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
    SystemError(std::error_code errorCode, FormatString fs, const Args &... args)
        : SystemError(DisambigVarArgs{}, errorCode, errorCode.message(), fs, args...)
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
 * `GetLastError()`). Like other errors, they derive from it through
 * `CloneableError`, e.g. `CloneableError<SysError,
 * NativeCodeSystemError<SysErrorFields, &SysErrorFields::errNo>>`, and inherit
 * its constructors.
 *
 * `Fields` must provide these static methods:
 *
 * - `ambientError()`, the ambient code, which the constructors not taking one
 *   use.
 *
 * - `toErrorCode(code)`, the `std::error_code` that an error for `code` has.
 *
 * - `renderError(code)`, the description of `code` that an error for it
 *   appends to its message.
 *
 * Together, `ambientError` and `toErrorCode` are for checking what went wrong,
 * e.g. by comparing with `std::errc`, where that is an expected outcome rather
 * than an exception, without constructing an error just to call
 * `SystemError::is`. If it does need throwing after all, pass the code to the
 * constructor taking one.
 *
 * The native code itself is stored in `Fields::*field`, e.g. `SysError::errNo`
 * or `WinError::lastError`. Each kind of code keeps its own name, so that code
 * using one where the other is meant, e.g. comparing a `WinError` code with an
 * `errno` value, does not mistakenly compile because of bad implicit
 * conversions.
 *
 * The field is in a separate base, `Fields`, rather than in the derived error
 * class itself, since that class's own members are not yet constructed when
 * this class's constructors set it.
 */
template<typename Fields, auto field>
    requires std::is_member_object_pointer_v<decltype(field)>
class NativeCodeSystemError : public Fields, public SystemError
{
public:
    /* `field` is a plain `auto`, constrained to be a pointer to a member,
       rather than `auto Fields::*`: GCC cannot deduce that placeholder,
       failing to use the preceding `Fields` for it. Applying `field` to a
       `Fields` here still checks that it is a member of `Fields`. */
    using NativeCode = std::remove_cvref_t<decltype(std::declval<Fields &>().*field)>;

    /**
     * Construct using the explicitly-provided error code, which
     * `Fields::renderError` describes in the message.
     */
    template<typename... Args>
    NativeCodeSystemError(NativeCode code, FormatString fs, const Args &... args)
        : Fields{code}
        , SystemError(DisambigVarArgs{}, Fields::toErrorCode(code), Fields::renderError(code), fs, args...)
    {
    }

    /**
     * Construct using the explicitly-provided error code, which
     * `Fields::renderError` describes in the message.
     *
     * Unlike above, the `HintFmt` already exists rather than being made on
     * the spot.
     */
    NativeCodeSystemError(NativeCode code, const HintFmt & hf)
        : Fields{code}
        , SystemError(DisambigHintFmt{}, Fields::toErrorCode(code), Fields::renderError(code), hf)
    {
    }

    /**
     * Construct using the ambient error code.
     *
     * Be sure to not perform another operation that may change it before
     * calling this constructor!
     */
    template<typename... Args>
    NativeCodeSystemError(FormatString fs, const Args &... args)
        : NativeCodeSystemError(Fields::ambientError(), fs, args...)
    {
    }

    /**
     * Construct using the ambient error code.
     *
     * Unlike above, the `HintFmt` already exists rather than being made on
     * the spot.
     */
    NativeCodeSystemError(const HintFmt & hf)
        : NativeCodeSystemError(Fields::ambientError(), hf)
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
     * rely on `NativeCodeSystemError(Fields::ambientError(), mkHintFmt())`
     * evaluating `Fields::ambientError()` first.
     */
    static std::pair<NativeCode, HintFmt> captureAmbientError(auto && mkHintFmt)
    {
        NativeCode e = Fields::ambientError();
        return {e, mkHintFmt()};
    }

    NativeCodeSystemError(std::pair<NativeCode, HintFmt> && p)
        : NativeCodeSystemError(p.first, std::move(p.second))
    {
    }
};

/**
 * The fields of `SysError` that `NativeCodeSystemError` sets, and the static
 * methods it needs for them.
 */
struct SysErrorFields
{
    int errNo;

    /**
     * The ambient `errno`. See `NativeCodeSystemError`.
     */
    static int ambientError()
    {
        return errno;
    }

    /**
     * See `NativeCodeSystemError`.
     */
    static std::error_code toErrorCode(int code)
    {
        return std::make_error_code(static_cast<std::errc>(code));
    }

    /**
     * With `strerror`. See `NativeCodeSystemError`.
     */
    static std::string renderError(int code)
    {
        return strerror(code);
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
class SysError final : public CloneableError<SysError, NativeCodeSystemError<SysErrorFields, &SysErrorFields::errNo>>
{
    void anchor() override;

public:
    using CloneableError::CloneableError;
};

#ifdef _WIN32

namespace windows {

/**
 * The fields of `WinError` that `NativeCodeSystemError` sets, and the static
 * methods it needs for them.
 */
struct WinErrorFields
{
    DWORD lastError;

    /**
     * The ambient `GetLastError()`. See `NativeCodeSystemError`.
     */
    static DWORD ambientError()
    {
        return GetLastError();
    }

    /**
     * See `NativeCodeSystemError`.
     */
    static std::error_code toErrorCode(DWORD code)
    {
        return std::error_code(code, std::system_category());
    }

    /**
     * With `FormatMessageA`. See `NativeCodeSystemError`.
     */
    static std::string renderError(DWORD code);
};

/**
 * Windows Error type.
 *
 * Unless you need to catch a specific error number, don't catch this in
 * portable code. Catch `SystemError` instead.
 */
class WinError : public CloneableError<WinError, NativeCodeSystemError<WinErrorFields, &WinErrorFields::lastError>>
{
    void anchor() override;

public:
    using CloneableError::CloneableError;
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

/**
 * The native error code that `NativeSysError` is for: an `errno` value on
 * Unix, and a `GetLastError()` value on Windows.
 */
using NativeError = NativeSysError::NativeCode;

} // namespace nix
