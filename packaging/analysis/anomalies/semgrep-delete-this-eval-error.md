# Exception: semgrep-delete-this-eval-error

## Finding
- **Tool**: semgrep
- **Check**: nix.store.delete-this
- **File**: src/libexpr/eval-error.cc (EvalErrorBuilder::debugThrow)
- **Severity**: warning

## Code
```cpp
template<class T>
void EvalErrorBuilder<T>::debugThrow()
{
    error.state.runDebugRepl(&error);

    // `EvalState` is the only class that can construct an `EvalErrorBuilder`,
    // and it does so in dynamic storage. This is the final method called on
    // any such instance and must delete itself before throwing the underlying
    // error.
    auto error = std::move(this->error);
    delete this;

    throw error;
}
```

## Analysis
The `delete this` pattern here is intentional and correct. `EvalErrorBuilder`
objects are always heap-allocated via `new` in `EvalState`. The `debugThrow()`
method is the terminal operation — it moves the error out, deletes the builder,
then throws. No member access occurs after `delete this`.

## Proposed Remediations

### Option A: Add nosemgrep comment
Add `// nosemgrep: nix.store.delete-this` on the `delete this;` line.

### Option B: Use std::unique_ptr factory
Return `std::unique_ptr<EvalErrorBuilder>` from the factory in `EvalState`:
```cpp
void debugThrow() {
    error.state.runDebugRepl(&error);
    auto error = std::move(this->error);
    // unique_ptr automatically deletes when it goes out of scope
    throw error;
}
```
Requires changing the builder API in EvalState.

### Option C: Keep exception (no code change)
The pattern is intentional and well-documented in comments. Suppress via triage exception only.

## Status
- **Reviewed by**: das
- **Date**: 2026-03-10
- **Decision**: Exception (Option C) — pending maintainer input
