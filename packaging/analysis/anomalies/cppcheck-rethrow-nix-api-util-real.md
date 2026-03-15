# Anomaly: cppcheck-rethrow-nix-api-util-real

## Finding
- **Tool**: cppcheck
- **Check**: rethrowNoCurrentException
- **File**: src/libutil-c/nix_api_util.cc:31
- **Severity**: error
- **Status**: confirmed-bug

## Code
```cpp
nix_err nix_context_error(nix_c_context * context)
{
    if (context == nullptr) {
        throw;  // <-- line 31: bare throw with no active exception
    }
    try {
        throw;  // line 34: OK — rethrows within catch context
    } catch (nix::Error & e) {
        // ...
    }
}
```

## Analysis
The `throw;` at line 31 is a bare rethrow statement that executes when
`context == nullptr`. This function is called from the `NIXC_CATCH_ERRS`
macro (which is inside a `catch (...)` block), so in normal flow an
exception is active. However:

1. If this function is ever called outside a catch block with a null context,
   `throw;` invokes `std::terminate()` — a hard crash with no error recovery.
2. Even in the intended usage, the semantics are confusing: the intent appears
   to be "propagate the error if we can't store it", but bare `throw;` in a
   function body is fragile and unclear.

This is in the C API boundary layer, making it especially important to handle
errors gracefully rather than crashing the host process.

## Proposed Remediations

### Option A: Throw a specific exception
```cpp
if (context == nullptr) {
    throw nix::Error("nix_context_error called with null context");
}
```
Clear error message, catchable by callers.

### Option B: Return an error code
```cpp
if (context == nullptr) {
    return NIX_ERR_UNKNOWN;
}
```
More appropriate for a C API boundary — no exceptions cross the FFI.

### Option C: Accept std::exception_ptr parameter
```cpp
nix_err nix_context_error(nix_c_context * context, std::exception_ptr eptr)
```
Eliminates all bare `throw;` statements. Requires updating the NIXC_CATCH_ERRS macro.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: confirmed-bug — recommend Option B for C API safety
