# Exception: cppcheck-rethrow-nix-api-util

## Finding
- **Tool**: cppcheck
- **Check**: rethrowNoCurrentException
- **File**: src/libutil-c/nix_api_util.cc (nix_context_error, line ~34)
- **Severity**: error

## Code
```cpp
nix_err nix_context_error(nix_c_context * context)
{
    if (context == nullptr) {
        throw;                    // line 31 — REAL bug (separate finding, NOT excepted)
    }
    try {
        throw;                    // line 34 — false positive (this exception)
    } catch (nix::Error & e) {
        context->last_err = e.what();
        // ...
    } catch (const std::exception & e) {
        // ...
    }
}
```

## Analysis
This function is always called from the `NIXC_CATCH_ERRS` macro, which expands to
`catch (...) { return nix_context_error(context); }`. The `try { throw; }` at line 34
is inside a valid exception context since the function is only reached from a catch block.

Note: The bare `throw;` at line 31 (inside the `if (context == nullptr)` branch) is
a **real bug** — if context is null and no exception is active, this is undefined
behavior. That finding is NOT excepted.

The code anchor `try {\n        throw;\n    } catch (nix::Error & e)` distinguishes
this false positive from the real bug at line 31.

## Proposed Remediations

### Option A: Add cppcheck inline suppression
Add `// cppcheck-suppress rethrowNoCurrentException` above line 34.

### Option B: Accept std::exception_ptr parameter
```cpp
nix_err nix_context_error(nix_c_context * context, std::exception_ptr eptr)
{
    if (context == nullptr)
        std::rethrow_exception(eptr);
    try {
        std::rethrow_exception(eptr);
    } catch (nix::Error & e) { ... }
}
```
Eliminates both the false positive and the real bug at line 31.

### Option C: Keep exception (no code change)
Suppress the line 34 finding via triage exception only.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: Exception (Option C) — pending maintainer input
