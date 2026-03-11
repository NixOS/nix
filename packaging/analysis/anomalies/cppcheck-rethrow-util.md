# Exception: cppcheck-rethrow-util

## Finding
- **Tool**: cppcheck
- **Check**: rethrowNoCurrentException
- **File**: src/libutil/util.cc (ignoreExceptionInDestructor)
- **Severity**: error

## Code
```cpp
void ignoreExceptionInDestructor(Verbosity lvl)
{
    try {
        try {
            throw;  // <-- flagged here
        } catch (Error & e) {
            printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.info().msg);
        } catch (std::exception & e) {
            printMsg(lvl, ANSI_RED "error (ignored):" ANSI_NORMAL " %s", e.what());
        }
    } catch (...) {
    }
}
```

## Analysis
cppcheck reports `rethrowNoCurrentException` because it sees `throw;` and cannot
determine that the function is always called from within a `catch` block. This is
a well-known cppcheck limitation — it does not perform interprocedural analysis to
verify that a current exception exists at the call site.

Every call site in the codebase invokes `ignoreExceptionInDestructor()` inside a
`catch (...)` handler, so there is always an active exception to rethrow.

## Proposed Remediations

### Option A: Add cppcheck inline suppression
Add `// cppcheck-suppress rethrowNoCurrentException` above the `throw;`.
Minimal change, targets cppcheck specifically.

### Option B: Restructure to use std::current_exception
Accept `std::exception_ptr` as a parameter:
```cpp
void ignoreExceptionInDestructor(std::exception_ptr eptr, Verbosity lvl)
{
    try {
        std::rethrow_exception(eptr);
    } catch (Error & e) { ... }
}
```
More explicit, satisfies multiple analyzers. Requires updating all call sites.

### Option C: Keep exception (no code change)
The pattern is idiomatic C++ and correct. Suppress via triage exception only.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: Exception (Option C) — pending maintainer input
