#pragma once
///@file

#include "nix/util/error.hh"

#include <boost/outcome.hpp>

#include <functional>
#include <type_traits>

namespace nix {

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

/**
 * Either a `T` or an error `E`, for functions whose failures are
 * expected and should be checked by the caller rather than thrown.
 *
 * @todo Consider C++23 `std::expected` instead. Then we could get rid
 * of `valueOr` because we would already have something similar from the
 * standard library.
 */
template<typename T, typename E>
using Result = outcome::unchecked<T, E>;

/**
 * The value of @p result, or else what @p f returns given its error.
 *
 * Usually @p f throws instead, e.g. a `NativeSysError` built from the
 * error with some context, in which case it need not return anything.
 */
template<typename T, typename E, typename F>
T valueOr(Result<T, E> && result, F && f)
{
    if (result)
        return std::move(result).value();
    if constexpr (std::is_void_v<std::invoke_result_t<F, E>>) {
        std::invoke(std::forward<F>(f), std::move(result).error());
        unreachable();
    } else
        return std::invoke(std::forward<F>(f), std::move(result).error());
}

} // namespace nix

/**
 * A handler for `nix::valueOr` that throws a `NativeSysError` from the
 * error, with the given format string and arguments.
 *
 * Example:
 *
 *     auto fd = valueOr(openFileReadonly(path), NIX_THROW_NATIVE_SYS_ERROR_HANDLER("opening file %s", PathFmt(path)));
 */
#define NIX_THROW_NATIVE_SYS_ERROR_HANDLER(...) [&](auto err__) { throw ::nix::NativeSysError(err__, __VA_ARGS__); }
