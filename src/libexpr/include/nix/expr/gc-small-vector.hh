#pragma once

#include <boost/container/small_vector.hpp>

#include "nix/expr/value.hh"

namespace nix {

/**
 * A GC compatible vector that may used a reserved portion of `nItems` on the stack instead of allocating on the heap.
 */
template<typename T, size_t nItems>
using SmallVector = boost::container::small_vector<T, nItems, traceable_allocator<T>>;

/**
 * A vector of value pointers. See `SmallVector`.
 */
template<size_t nItems>
using SmallValueVector = SmallVector<Value *, nItems>;

/**
 * A vector of values that must not be referenced after the vector is destroyed.
 *
 * See also `SmallValueVector`.
 */
template<size_t nItems>
using SmallTemporaryValueVector = SmallVector<Value, nItems>;

} // namespace nix
