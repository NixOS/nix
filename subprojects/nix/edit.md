R""(

# Examples

* Open the Nix expression of the GNU Hello package:

  ```console
  # nix edit nixpkgs#hello
  ```

* Get the filename and line number used by `nix edit`:

  ```console
  # nix eval --raw nixpkgs#hello.meta.position
  /nix/store/fvafw0gvwayzdan642wrv84pzm5bgpmy-source/pkgs/applications/misc/hello/default.nix:15
  ```

# Description

This command opens the Nix expression of a derivation in an
editor. The filename and line number of the derivation are taken from
its `meta.position` attribute. Nixpkgs' `stdenv.mkDerivation` sets
this attribute to the location of the definition of the
`meta.description`, `version` or `name` derivation attributes.

The editor to invoke is specified by the `EDITOR` environment
variable. It defaults to `cat`. If the editor is `emacs`, `nano`,
`vim` or `kak`, it is passed the line number of the derivation using
the argument `+<lineno>`.

)""
