---
synopsis: Introduction of `--regex` and `--all` in `nix profile remove` and `nix profile upgrade`
prs: 10166
---

Previously the command-line arguments for `nix profile remove` and `nix profile upgrade` matched the package entries using regular expression.
For instance:

```
nix profile remove '.*vim.*'
```

This would remove all packages that contain `vim` in their name.

In most cases, only singular package names were used to remove and upgrade packages. Mixing this with regular expressions sometimes lead to unintended behavior. For instance, `python3.1` could match `python311`.

To avoid unintended behavior, the arguments are now only matching exact names.

Matching using regular expressions is still possible by using the new `--regex` flag:

```
nix profile remove --regex '.*vim.*'
```

One of the most useful cases for using regular expressions was to upgrade all packages. This was previously accomplished by:

```
nix profile upgrade '.*'
```

With the introduction of the `--all` flag, this now becomes more straightforward:

```
nix profile upgrade --all
```
