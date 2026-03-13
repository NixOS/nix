// Test corpus for nix-verify-rethrow-context
// RUN: clang-tidy --checks='-*,nix-verify-rethrow-context' %s -- -std=c++20

#include <stdexcept>

// Case 1: throw; inside catch -> VERIFIED
void rethrowInCatch() {
  try {
    throw std::runtime_error("test");
  } catch (...) {
    throw; // VERIFIED: inside catch block
  }
}

// Case 2: throw; inside try but called from catch context -> INCONCLUSIVE
void helperThatRethrows() {
  try {
    throw; // INCONCLUSIVE: in try block but not in catch
  } catch (...) {
    // handle
  }
}

// Case 3: throw; with no try/catch -> CONTRADICTION
void bareRethrow() {
  throw; // CONTRADICTION: no catch block, will call std::terminate
}

// Case 4: Nested try/catch — throw in inner catch -> VERIFIED
void nestedRethrow() {
  try {
    try {
      throw std::runtime_error("inner");
    } catch (const std::runtime_error &) {
      throw; // VERIFIED: inside catch block
    }
  } catch (...) {
    // outer handler
  }
}
