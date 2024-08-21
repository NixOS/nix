---
synopsis: Define integer overflow in the Nix language as an error
issues: [10968]
prs: [11188]
---

Previously, integer overflow in the Nix language invoked C++ level signed overflow, which was undefined behaviour, but *usually* manifested as wrapping around on overflow.

Since prior to the public release of Lix, Lix had C++ signed overflow defined to crash the process and nobody noticed this having accidentally removed overflow from the Nix language for three months until it was caught by fiddling around.
Given the significant body of actual Nix code that has been evaluated by Lix in that time, it does not appear that nixpkgs or much of importance depends on integer overflow, so it appears safe to turn into an error.

Some other overflows were fixed:
- `builtins.fromJSON` of values greater than the maximum representable value in a signed 64-bit integer will generate an error.
- `nixConfig` in flakes will no longer accept negative values for configuration options.

Integer overflow now looks like the following:

```
$ nix eval --expr '9223372036854775807 + 1'
error: integer overflow in adding 9223372036854775807 + 1
```
