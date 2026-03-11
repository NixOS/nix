# Anomaly: cppcheck-uninitMemberVar-DrvName

## Finding
- **Tool**: cppcheck
- **Check**: uninitMemberVar
- **File**: src/libstore/include/nix/store/names.hh:17, src/libstore/names.cc:13
- **Severity**: warning
- **Status**: confirmed-bug

## Code
```cpp
// names.hh
struct DrvName
{
    std::string fullName;
    std::string name;
    std::string version;
    unsigned int hits;   // <-- no in-class initializer
    // ...
};

// names.cc
DrvName::DrvName()       // Default constructor
{
    name = "";           // Only initializes name, NOT hits
}

DrvName::DrvName(std::string_view s)
    : hits(0)            // Parameterized constructor correctly initializes hits
{
    // ...
}
```

## Analysis
The default constructor `DrvName::DrvName()` does not initialize the `hits`
member. Since `hits` is `unsigned int` (a POD type), it will contain whatever
garbage was in memory. The parameterized constructor correctly initializes
`hits(0)`, confirming the intent is for `hits` to start at zero.

Any code path that constructs a `DrvName` via the default constructor and
later reads `hits` will get undefined behavior. While the default constructor
appears to be rarely used (most callers use the `std::string_view` constructor),
the bug is still present and could manifest as non-deterministic behavior in
package name matching or sorting.

## Proposed Remediations

### Option A: Add in-class initializer (recommended)
```cpp
unsigned int hits = 0;
```
One-line fix in the header. Covers all constructors automatically.

### Option B: Initialize in the default constructor
```cpp
DrvName::DrvName() : hits(0) {
    name = "";
}
```
Fixes the specific constructor but doesn't protect against future constructors.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: confirmed-bug — recommend Option A (in-class initializer)
