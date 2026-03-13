// Test corpus for nix-verify-delete-this-safety
// RUN: clang-tidy --checks='-*,nix-verify-delete-this-safety' %s -- -std=c++20

#include <stdexcept>
#include <utility>

// Case 1: Safe delete-this — no post-delete this access -> VERIFIED
class SafeDeleteThis {
  std::runtime_error error;

public:
  SafeDeleteThis(std::runtime_error e) : error(std::move(e)) {}

  [[noreturn]] void throwAndDelete() {
    auto localError = std::move(this->error);
    delete this; // VERIFIED: only local var used after delete
    throw localError;
  }
};

// Case 2: Unsafe delete-this — accesses member after delete -> CONTRADICTION
class UnsafeDeleteThis {
  int value;

public:
  int getValueAndDelete() {
    delete this; // CONTRADICTION: this->value accessed after delete
    return this->value;
  }
};

// Case 3: Safe delete-this at end of function -> VERIFIED
class DeleteThisAtEnd {
public:
  void destroy() {
    // Do cleanup
    delete this; // VERIFIED: nothing follows
  }
};
