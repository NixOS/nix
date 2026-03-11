# Anomaly: clang-tidy-move-forwarding-ref-diagnose

## Finding
- **Tool**: clang-tidy
- **Check**: bugprone-move-forwarding-reference
- **File**: src/libexpr/include/nix/expr/diagnose.hh:71
- **Severity**: warning
- **Status**: false-positive

## Code
```cpp
auto withError = [&](bool fatal, auto && handler) {
    auto maybeError = mkError(fatal);
    // ...
    handler(std::move(*maybeError));  // always passes rvalue
};

case Diagnose::Fatal:
    withError(true, [](auto && error) {
        throw std::move(error);       // <-- flagged here (line 71)
    });
```

## Analysis
clang-tidy warns that `std::move(error)` on a forwarding reference could
unexpectedly move an lvalue. However, `error` always binds to an rvalue
because `handler(std::move(*maybeError))` passes an rvalue on line 61.
The `std::move` on line 71 is redundant (error is already an rvalue reference)
but not harmful.

Technically, `std::forward<decltype(error)>(error)` would be more correct,
but the behavior is identical in this context since the argument is always
an rvalue.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: false-positive — handler always receives rvalue
