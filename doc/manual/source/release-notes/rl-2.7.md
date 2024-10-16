# Release 2.7 (2022-03-07)

* Nix will now make some helpful suggestions when you mistype
  something on the command line. For instance, if you type `nix build
  nixpkgs#thunderbrd`, it will suggest `thunderbird`.

* A number of "default" flake output attributes have been
  renamed. These are:

  * `defaultPackage.<system>` → `packages.<system>.default`
  * `defaultApps.<system>` → `apps.<system>.default`
  * `defaultTemplate` → `templates.default`
  * `defaultBundler.<system>` → `bundlers.<system>.default`
  * `overlay` → `overlays.default`
  * `devShell.<system>` → `devShells.<system>.default`

  The old flake output attributes still work, but `nix flake check`
  will warn about them.

* Breaking API change: `nix bundle` now supports bundlers of the form
  `bundler.<system>.<name>= derivation: another-derivation;`. This
  supports additional functionality to inspect evaluation information
  during bundling. A new
  [repository](https://github.com/NixOS/bundlers) has various bundlers
  implemented.

* `nix store ping` now reports the version of the remote Nix daemon.

* `nix flake {init,new}` now display information about which files have been
  created.

* Templates can now define a `welcomeText` attribute, which is printed out by
  `nix flake {init,new} --template <template>`.
