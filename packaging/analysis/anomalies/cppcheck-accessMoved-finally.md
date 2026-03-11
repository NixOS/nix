# Exception: cppcheck-accessMoved-finally

## Finding
- **Tool**: cppcheck
- **Check**: accessMoved
- **File**: src/libutil/include/nix/util/finally.hh
- **Severity**: warning

## Code
```cpp
Finally(Finally && other) noexcept(std::is_nothrow_move_constructible_v<Fn>)
    : fun(std::move(other.fun))
{
    other.movedFrom = true;
}

~Finally() noexcept(false)
{
    try {
        if (!movedFrom)
            fun();
    } catch (...) { ... }
}
```

## Analysis
cppcheck flags `other.movedFrom = true` as accessing a moved-from object, because
`other.fun` was just moved on the line above. However, setting `movedFrom` on the
moved-from object is the entire point — it's a flag that prevents the destructor of
the moved-from `Finally` from calling the (now-empty) function. This is correct
move semantics with a moved-from guard.

## Proposed Remediations

### Option A: Add cppcheck inline suppression
Add `// cppcheck-suppress accessMoved` above `other.movedFrom = true;`.

### Option B: Use std::optional<Fn> instead of movedFrom flag

This refactor eliminates the `movedFrom` flag entirely. The `std::optional`
tracks validity, and `std::move` + `reset()` is the standard pattern.

```diff
 template<typename Fn>
 class [[nodiscard("Finally values must be used")]] Finally
 {
 private:
-    Fn fun;
-    bool movedFrom = false;
+    std::optional<Fn> fun;

 public:
     Finally(Fn fun)
-        : fun(std::move(fun))
+        : fun(std::in_place, std::move(fun))
     {
     }

     Finally(Finally & other) = delete;

     Finally(Finally && other) noexcept(std::is_nothrow_move_constructible_v<Fn>)
         : fun(std::move(other.fun))
     {
-        other.movedFrom = true;
+        other.fun.reset();
     }

     ~Finally() noexcept(false)
     {
         try {
-            if (!movedFrom)
-                fun();
+            if (fun)
+                (*fun)();
         } catch (...) {
```

Eliminates the need for a separate `movedFrom` flag and silences the
cppcheck `accessMoved` diagnostic without suppression comments.

### Option C: Keep exception (no code change)
The pattern is correct and idiomatic. Suppress via triage exception only.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: Exception (Option C) — pending maintainer input
