---
synopsis: C API `nix_flake_init_global` removed
prs: 12759
issues: 5638
---

In order to improve the modularity of the code base, we are removing a use of global state, and therefore the `nix_flake_init_global` function.

Instead, use `nix_flake_settings_add_to_eval_state_builder`. For example:

```diff
-    nix_flake_init_global(ctx, settings);
-    HANDLE_ERROR(ctx);
-
     nix_eval_state_builder * builder = nix_eval_state_builder_new(ctx, store);
     HANDLE_ERROR(ctx);
 
+    nix_flake_settings_add_to_eval_state_builder(ctx, settings, builder);
+    HANDLE_ERROR(ctx);
```
