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

/**
 * For functions where we do not expect deep recursion, we can use a sizable
 * part of the stack a free allocation space.
 *
 * Note: this is expected to be multiplied by sizeof(Value), or about 24 bytes.
 */
constexpr size_t nonRecursiveStackReservation = 128;

/**
 * Functions that maybe applied to self-similar inputs, such as concatMap on a
 * tree, should reserve a smaller part of the stack for allocation.
 *
 * Note: this is expected to be multiplied by sizeof(Value), or about 24 bytes.
 */
constexpr size_t conservativeStackReservation = 16;

} // namespace nix
