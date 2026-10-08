---
synopsis: "C API: `nix_eval_state_builder_set_realise_hook`"
prs: [16683]
---

The new function `nix_eval_state_builder_set_realise_hook()` in `nix_api_expr.h` installs a callback that realises the
derivations of an import from derivation in place of the store.
An embedder with a build system of its own can build them there; the outputs only have to be valid in the
evaluation store when the callback returns.
In C++, the same hook is `EvalSettings::realiseHook`.
