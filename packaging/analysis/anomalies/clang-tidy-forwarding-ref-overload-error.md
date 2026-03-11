# Anomaly: clang-tidy-forwarding-ref-overload-error

## Finding
- **Tool**: clang-tidy
- **Check**: bugprone-forwarding-reference-overload
- **File**: src/libutil/include/nix/util/error.hh:426
- **Severity**: warning
- **Status**: false-positive

## Code
```cpp
SysError(auto && mkHintFmt)
    requires std::invocable<decltype(mkHintFmt)> &&
             std::same_as<std::invoke_result_t<decltype(mkHintFmt)>, HintFmt>
    : SysError(captureErrno(std::forward<decltype(mkHintFmt)>(mkHintFmt)))
{
}
```

## Analysis
clang-tidy warns that a forwarding reference constructor can hide copy/move
constructors. This is normally a valid concern, but the C++20 `requires` clause
constrains this constructor to only match types that are:
1. Invocable (callable with no arguments)
2. Return `HintFmt`

This means `SysError(const SysError&)` and `SysError(SysError&&)` are NOT
matched by this template — a `SysError` object is not invocable, so the
constraint fails and the copy/move constructors are used instead.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: false-positive — C++20 requires clause prevents hijacking
