---
synopsis: Coercion errors include the failing value
issues: #561
prs: #9754
---

The `error: cannot coerce a <TYPE> to a string` message now includes the value
which caused the error.

Before:

```
       error: cannot coerce a set to a string
```

After:

```
       error: cannot coerce a set to a string: { aesSupport = «thunk»;
       avx2Support = «thunk»; avx512Support = «thunk»; avxSupport = «thunk»;
       canExecute = «thunk»; config = «thunk»; darwinArch = «thunk»; darwinMinVersion
       = «thunk»; darwinMinVersionVariable = «thunk»; darwinPlatform = «thunk»; «84
       attributes elided»}
```
