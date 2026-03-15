# Anomaly: cppcheck-unsignedLessThanZero-serialise

## Finding
- **Tool**: cppcheck
- **Check**: unsignedLessThanZero
- **File**: src/libutil/include/nix/util/serialise.hh:393
- **Severity**: style
- **Status**: confirmed-bug

## Code
```cpp
struct SizedSource : Source
{
    Source & orig;
    size_t remain;          // <-- unsigned type

    size_t read(char * data, size_t len) override
    {
        if (this->remain <= 0) {      // <-- comparing unsigned <= 0
            throw EndOfFile("sized: unexpected end-of-file");
        }
        len = std::min(len, this->remain);
        size_t n = this->orig.read(data, len);
        this->remain -= n;
        return n;
    }
};
```

## Analysis
`remain` is `size_t` (unsigned). The comparison `remain <= 0` is equivalent
to `remain == 0` because an unsigned value can never be less than zero. The
`< 0` part of the condition is dead code.

This suggests either:
1. The developer intended to detect underflow (impossible with unsigned), or
2. The comparison should simply be `== 0` to express the actual intent.

The `std::min` on the next line prevents `n` from exceeding `remain`, so
the subtraction `remain -= n` won't wrap around in practice. But the
tautological comparison indicates confused intent about the type's range.

## Proposed Remediations

### Option A: Change to == 0 (recommended)
```cpp
if (this->remain == 0) {
```
Expresses the actual intent clearly. Minimal change.

### Option B: Use signed type
Change `remain` to `ssize_t` or `ptrdiff_t` to allow negative values
and genuine underflow detection. This changes the API semantics.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: confirmed-bug — recommend Option A (change to == 0)
