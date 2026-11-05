#pragma once
///@file

#include <boost/outcome.hpp>

namespace nix {

namespace outcome = BOOST_OUTCOME_V2_NAMESPACE;

/**
 * Either a `T` or an error `E`, for functions whose failures are
 * expected and should be checked by the caller rather than thrown.
 */
template<typename T, typename E>
using Result = outcome::unchecked<T, E>;

} // namespace nix
