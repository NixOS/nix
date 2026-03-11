# Anomaly: cppcheck-missingOverride-Command

## Finding
- **Tool**: cppcheck
- **Check**: missingOverride
- **File**: src/libutil/include/nix/util/args.hh:370
- **Severity**: style
- **Status**: needs-review

## Code
```cpp
struct Command : virtual public Args
{
    virtual ~Command() = default;  // should be: ~Command() override = default;
};
```

## Analysis
`Command` inherits from `Args`, which has a virtual destructor. The `Command`
destructor overrides it but uses `virtual` instead of `override`. Modern C++
best practice (and clang-tidy's `modernize-use-override`) recommends using
`override` for clarity and to catch signature mismatches at compile time.

Note: `virtual` on an overriding destructor is redundant — it's already virtual
by inheritance.

## Proposed Remediations
```cpp
~Command() override = default;
```

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: needs-review — trivial fix, low risk
