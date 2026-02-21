---
synopsis: "C API: Errors returned from your primops are not treated as recoverable by default"
prs: [15286, 13930]
---

Nix 2.34 by default remembers the error in the thunk that triggered it.

Previously the following sequence of events worked:

1. Have a thunk that invokes a primop that's defined through the C API
2. The primop returns an error
3. Force the thunk again
4. The primop returns a value
5. The thunk evaluated successfully

**Resolution**

C API consumers that rely on this must change their recoverable error calls:

```diff
-nix_set_err_msg(context, NIX_ERR_*, msg);
+nix_set_err_msg(context, NIX_ERR_RECOVERABLE, msg);
```
