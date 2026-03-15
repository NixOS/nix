// Test corpus for nix-verify-move-from-flag
// RUN: clang-tidy --checks='-*,nix-verify-move-from-flag' %s -- -std=c++20

#include <functional>
#include <utility>

// Case 1: Bool flag assignment in move ctor -> VERIFIED
class WithBoolFlag {
  std::function<void()> fun;
  bool movedFrom = false;

public:
  WithBoolFlag(std::function<void()> f) : fun(std::move(f)) {}

  WithBoolFlag(WithBoolFlag &&other) noexcept
      : fun(std::move(other.fun)), movedFrom(other.movedFrom) {
    other.movedFrom = true; // VERIFIED: bool flag pattern
  }

  ~WithBoolFlag() {
    if (!movedFrom && fun)
      fun();
  }
};

// Case 2: Non-bool assignment in move ctor -> CONTRADICTION
class WithNonBoolAccess {
  int count = 0;
  int *data = nullptr;

public:
  WithNonBoolAccess(WithNonBoolAccess &&other) noexcept
      : data(std::exchange(other.data, nullptr)) {
    other.count = 42; // CONTRADICTION: non-bool member access after move
  }
};
