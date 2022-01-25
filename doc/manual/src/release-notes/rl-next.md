# Release X.Y (202?-??-??)

* `nix bundle` breaking API change now supports bundlers of the form
  `bundler.<system>.<name>= derivation: another-derivation;`. This supports
  additional functionality to inspect evaluation information during bundling. A
  new [repository](https://github.com/NixOS/bundlers) has various bundlers
  implemented.

