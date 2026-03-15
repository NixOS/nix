// Test corpus for nix-verify-constrained-ctor
// RUN: clang-tidy --checks='-*,nix-verify-constrained-ctor' %s -- -std=c++20

#include <type_traits>
#include <utility>

// Case 1: Constructor with requires clause -> VERIFIED
template<typename T>
class WithConstraint {
public:
  template<typename U>
    requires std::is_convertible_v<U, T>
  WithConstraint(U &&val) : value(static_cast<T>(std::forward<U>(val))) {}
  // VERIFIED: has requires clause

  WithConstraint(const WithConstraint &) = default;
  WithConstraint(WithConstraint &&) = default;

private:
  T value;
};

// Case 2: Constructor without requires clause -> CONTRADICTION
template<typename T>
class WithoutConstraint {
public:
  template<typename U>
  WithoutConstraint(U &&val) : value(static_cast<T>(std::forward<U>(val))) {}
  // CONTRADICTION: no requires clause

  WithoutConstraint(const WithoutConstraint &) = default;
  WithoutConstraint(WithoutConstraint &&) = default;

private:
  T value;
};

// Case 3: Non-template constructor with rvalue ref (not a forwarding ref) -> CONTRADICTION
// (But this is a concrete rvalue reference, not truly a forwarding ref issue)
class ConcreteRvalueRef {
public:
  ConcreteRvalueRef(int &&val) : value(val) {}
  // CONTRADICTION: has rvalue ref param with no constraint

private:
  int value;
};
