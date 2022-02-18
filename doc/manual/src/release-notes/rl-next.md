# Release X.Y (202?-??-??)

* `nix bundle` breaking API change now supports bundlers of the form
  `bundler.<system>.<name>= derivation: another-derivation;`. This supports
  additional functionality to inspect evaluation information during bundling. A
  new [repository](https://github.com/NixOS/bundlers) has various bundlers
  implemented.

* `nix store ping` now reports the version of the remote Nix daemon.

* `nix flake {init,new}` now display information about which files have been
  created.

* Templates can now define a `welcomeText` attribute, which is printed out by
  `nix flake {init,new} --template <template>`.
