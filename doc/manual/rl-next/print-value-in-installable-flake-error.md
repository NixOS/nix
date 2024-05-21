---
synopsis: New-cli flake commands that expect derivations now print the failing value and its type
prs: 10778
---

In errors like `flake output attribute 'nixosConfigurations.yuki.config' is not a derivation or path`, the message now includes the failing value and type.

Before:

```
    error: flake output attribute 'nixosConfigurations.yuki.config' is not a derivation or path
````

After:

```
    error: expected flake output attribute 'nixosConfigurations.yuki.config' to be a derivation or path but found a set: { appstream = «thunk»; assertions = «thunk»; boot = { bcache = «thunk»; binfmt = «thunk»; binfmtMiscRegistrations = «thunk»; blacklistedKernelModules = «thunk»; bootMount = «thunk»; bootspec = «thunk»; cleanTmpDir = «thunk»; consoleLogLevel = «thunk»; «43 attributes elided» }; «48 attributes elided» }
```
