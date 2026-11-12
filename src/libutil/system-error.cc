#include "nix/util/system-error.hh"

namespace nix {

void SystemError::anchor() {}

void SysError::anchor() {}

/**
 * A `SystemError` for a particular kind of native error code, as
 * `SysError` (for `errno`) and `windows::WinError` (for
 * `GetLastError()`) are.
 *
 * Such a type, like those implemented with `NativeCodeSystemError`, has:
 *
 * - Constructors taking an `E::NativeCode` explicitly, or the ambient
 *   one implicitly, and a `HintFmt` or a format string and its
 *   arguments. (The variadic ones can only be checked for some example
 *   arguments, and would accept most anything anyway.)
 *
 * - `E::ambientError()`, the ambient code, which the constructors not
 *   taking one use.
 *
 * - `E::toErrorCode(code)`, the `std::error_code` that an `E` for
 *   `code` has.
 *
 * - `E::renderError(code)`, the description of `code` that an `E` for
 *   it appends to its message.
 *
 * See `NativeCodeSystemError` for what these are for.
 */
template<typename E>
concept SystemErrorWithNativeCode =
    std::derived_from<E, SystemError> && std::constructible_from<E, typename E::NativeCode, const HintFmt &>
    && std::constructible_from<E, const HintFmt &>
    // The variadic constructors, for an example format string and arguments.
    && std::constructible_from<E, typename E::NativeCode, FormatString<2>, int, bool>
    && std::constructible_from<E, FormatString<2>, int, bool>
    // Also these methods
    && requires(typename E::NativeCode code) {
           { E::ambientError() } -> std::same_as<typename E::NativeCode>;
           { E::toErrorCode(code) } -> std::same_as<std::error_code>;
           { E::renderError(code) } -> std::same_as<std::string>;
       };

static_assert(SystemErrorWithNativeCode<SysError>);
#ifdef _WIN32
static_assert(SystemErrorWithNativeCode<windows::WinError>);
#endif

} // namespace nix
