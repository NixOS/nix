---
synopsis: "C API: Fix EvalState pointer passed to primop callbacks"
issues: []
prs: []
---

The `EvalState *` passed to C API primop callbacks was incorrectly pointing to
the internal `nix::EvalState` rather than the C API wrapper struct. This caused
a segfault when the callback used the pointer with C API functions such as
`nix_alloc_value()`. The same issue affected `printValueAsJSON` and
`printValueAsXML` callbacks on external values.

The fix creates a proper non-owning `EvalState` wrapper for each callback
invocation, which also works correctly for C plugins where the
`nix::EvalState` was created by C++ code rather than the C API.
