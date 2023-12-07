---
synopsis: Type errors include the failing value
issues: #561
prs: #9753
---

In errors like `value is an integer while a list was expected`, the message now
includes the failing value.

Before:

```
       error: value is a set while a string was expected
```

After:

```
       error: expected a string but found a set: { ghc810 = «thunk»;
       ghc8102Binary = «thunk»; ghc8107 = «thunk»; ghc8107Binary = «thunk»;
       ghc865Binary = «thunk»; ghc90 = «thunk»; ghc902 = «thunk»; ghc92 = «thunk»;
       ghc924Binary = «thunk»; ghc925 = «thunk»;  «17 attributes elided»}
```
