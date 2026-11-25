---
synopsis: C API functions now do not unwind into the caller on errors with a NULL context
packages: [c-api]
prs: [16891]
---

Previously, C API functions, when called with NULL `context` parameter, would throw a C++ exception on errors, causing stack unwinding into the caller stack frames. Now such errors are reported by terminating the process.

The prior behavior is unwanted for several reasons:

- For callers across the FFI boundary it would often be equivalent to terminating the process, albeit in
  an unsafe manner by unwinding into `main` or thread entry functions (e.g. when called from a C program).
- Rust library consumers would need to make use of the `extern "C-unwind"` ABI to avoid undefined behavior, which none of the existing consumers were deemed to make use of.

API consumers should prefer to always pass in a `nix_c_context` for error handling and reporting errors from callbacks via the supplied `nix_c_context`, such as in `PrimOpFun`.

`nix_get_string_callback` callbacks don't need any error reporting facilities and must not make use of stack unwinding. Throwing C++ exceptions or `panic`-ing in those callbacks will now terminate the program.
