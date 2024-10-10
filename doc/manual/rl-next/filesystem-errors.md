---
synopsis: wrap filesystem exceptions more correctly
issues: []
prs: [11378]
---


With the switch to `std::filesystem` in different places, Nix started to throw `std::filesystem::filesystem_error` in many places instead of its own exceptions.

This lead to no longer generating error traces, for example when listing a non-existing directory, and can also lead to crashes inside the Nix REPL.

This version catches these types of exception correctly and wrap them into Nix's own exeception type.

Author: [**@Mic92**](https://github.com/Mic92)
