# Exception: cppcheck-syntaxError-base-nix-32

## Finding
- **Tool**: cppcheck
- **Check**: syntaxError
- **File**: src/libutil/base-nix-32.cc
- **Severity**: error

## Code
```cpp
constexpr const std::array<unsigned char, 256> BaseNix32::reverseMap = [] {
    std::array<unsigned char, 256> map{};
    for (size_t i = 0; i < map.size(); ++i)
        map[i] = invalid;
    for (unsigned char i = 0; i < 32; ++i)
        map[static_cast<unsigned char>(characters[i])] = i;
    return map;
}();
```

## Analysis
cppcheck's parser does not fully support C++20 constexpr lambdas used as
initializers for static class members. The code is valid C++20 and compiles
correctly with both GCC and Clang. This is a known cppcheck parser limitation.

## Proposed Remediations

### Option A: Add cppcheck inline suppression
Add `// cppcheck-suppress syntaxError` above the constexpr line.

### Option B: Move to a constexpr function
```cpp
static constexpr std::array<unsigned char, 256> makeReverseMap() {
    std::array<unsigned char, 256> map{};
    // ... same logic ...
    return map;
}
constexpr const std::array<unsigned char, 256> BaseNix32::reverseMap = makeReverseMap();
```
Uses a named function instead of a lambda, which cppcheck can parse.

### Option C: Keep exception (no code change)
The code is valid C++20. Suppress via triage exception only.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: Exception (Option C) — pending maintainer input
