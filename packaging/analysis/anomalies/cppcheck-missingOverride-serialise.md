# Anomaly: cppcheck-missingOverride-serialise

## Finding
- **Tool**: cppcheck
- **Check**: missingOverride
- **File**: src/libutil/include/nix/util/serialise.hh (multiple locations)
- **Severity**: style
- **Status**: needs-review

## Affected Destructors

| Class | Line | Current | Suggested |
|-------|------|---------|-----------|
| FdSink | 190 | `~FdSink();` | `~FdSink() override;` |
| FdSource | 224 | `~FdSource() = default;` | `~FdSource() override = default;` |
| LambdaSink | 475 | `~LambdaSink()` | `~LambdaSink() override` |
| FramedSource | 677 | `~FramedSource()` | `~FramedSource() override` |
| FramedSink | 739 | `~FramedSink()` | `~FramedSink() override` |

## Analysis
All five classes inherit from base classes with virtual destructors (Sink,
BufferedSink, Source, BufferedSource) but don't mark their overriding
destructors with `override`. This is a code quality issue — the destructors
work correctly, but `override` provides compile-time checking that the
base class destructor is indeed virtual.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: needs-review — batch fix, low risk
