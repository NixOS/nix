// Test corpus for nix-lock-guard-temporary check.
// RUN: clang-tidy --checks='-*,nix-lock-guard-temporary' %s --
//
// Positive cases (should warn):
//   // CHECK-MESSAGES: :[[@LINE+1]]:{{.*}} warning: lock guard constructed as a temporary
//
// Negative cases (should NOT warn):
//   No CHECK-MESSAGES expected.

#include <mutex>

std::mutex mtx;
std::mutex mtx2;

// --- Positive cases: temporaries (immediately destroyed) ---
// Use brace-init syntax — parentheses trigger most-vexing-parse.

void bad_lock_guard()
{
    std::lock_guard<std::mutex>{mtx}; // warn
}

void bad_scoped_lock()
{
    std::scoped_lock<std::mutex>{mtx}; // warn
}

void bad_unique_lock()
{
    std::unique_lock<std::mutex>{mtx}; // warn
}

// --- Negative cases: named variables (lock held until scope exit) ---

void good_lock_guard()
{
    std::lock_guard<std::mutex> lock(mtx); // ok
}

void good_scoped_lock()
{
    std::scoped_lock lock(mtx); // ok
}

void good_unique_lock()
{
    std::unique_lock<std::mutex> lock(mtx); // ok
    lock.unlock();
}

void good_unique_lock_deferred()
{
    std::unique_lock<std::mutex> lock(mtx, std::defer_lock); // ok
    lock.lock();
}
