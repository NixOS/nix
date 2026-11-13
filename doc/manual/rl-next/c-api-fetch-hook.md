---
synopsis: "C API: `nix_eval_state_builder_set_fetch_hook`"
prs: [16684]
---

The new function `nix_eval_state_builder_set_fetch_hook()` in `nix_api_expr.h` installs a callback that fetches the
inputs an evaluation needs and does not find in the store: the inputs of `fetchTree` other than `path`, and the
downloads of `fetchurl` and `fetchTarball`.
The callback receives the fetch request as JSON and answers with the store path it added.
In C++, the same hook is `fetchers::Settings::fetchHook`.
