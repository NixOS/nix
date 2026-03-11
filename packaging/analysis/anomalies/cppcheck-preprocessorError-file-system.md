# Exception: cppcheck-preprocessorError-file-system

## Finding
- **Tool**: cppcheck
- **Check**: preprocessorErrorDirective
- **File**: src/libutil/include/nix/util/file-system.hh
- **Severity**: error

## Code
```cpp
#ifndef S_IFLNK
#  ifndef _WIN32
#    error "S_IFLNK should be defined on non-Windows platforms"
#  endif
#  define S_IFLNK 0120000
#endif
```

## Analysis
cppcheck reports `preprocessorErrorDirective` because it encounters the `#error`
directive. This is a conditional error that only fires on non-Windows platforms where
`S_IFLNK` is unexpectedly undefined — a build configuration sanity check. It never
triggers during normal builds on supported platforms. cppcheck doesn't evaluate the
full preprocessor condition chain and flags the `#error` unconditionally.

## Proposed Remediations

### Option A: Add cppcheck inline suppression
Add `// cppcheck-suppress preprocessorErrorDirective` above the `#error`.

### Option B: Wrap in cppcheck guard
```cpp
#ifndef __cppcheck__
#    error "S_IFLNK should be defined on non-Windows platforms"
#endif
```
Hides the directive from cppcheck while preserving it for real compilers.

### Option C: Keep exception (no code change)
The `#error` is an intentional build guard. Suppress via triage exception only.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: Exception (Option C) — pending maintainer input
