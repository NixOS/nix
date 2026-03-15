# Anomaly: cppcheck-accessMoved-finally-dtor

## Finding
- **Tool**: cppcheck
- **Check**: accessMoved
- **File**: src/libutil/include/nix/util/finally.hh:40
- **Severity**: warning
- **Status**: false-positive

## Code
```cpp
~Finally() noexcept(false)
{
    try {
        if (!movedFrom)     // line 39: guard check
            fun();          // line 40: only called if NOT moved from
    } catch (...) {
        // ...
    }
}
```

## Analysis
cppcheck flags `fun()` at line 40 as accessing a moved-from object because
the move constructor does `fun(std::move(other.fun))`. However, the destructor
explicitly guards the call with `if (!movedFrom)`. If `this` object had its
`fun` moved away, `movedFrom` would be `true` and `fun()` would never be called.

This is the same pattern as the constructor-side finding (cppcheck-accessMoved-finally-ctor)
but reported at the destructor call site instead.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: false-positive — movedFrom guard prevents use-after-move
