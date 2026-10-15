---
synopsis: "`builtins.addErrorContext` keeps the error from *value* when *context* fails"
issues: 8283
prs: [16618]
---

If evaluating *context* in [`builtins.addErrorContext context value`](@docroot@/language/builtins.md#builtins-addErrorContext) fails, the error from *value* now propagates, with the error from *context* in its trace.
Library authors can therefore use more complex *context* expressions without the risk of hiding the error from *value*.

`addErrorContext` can no longer be abused to catch uncatchable errors like `abort`.
