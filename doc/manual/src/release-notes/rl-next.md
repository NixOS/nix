# Release X.Y (202?-??-??)

* `nix store ping` now reports the version of the remote Nix daemon.

* The nixConfig attribute of flakes can now provide platform specific options
  like the following:

       nixConfig.perSystem.aarch64-darwin.extra-platforms = [ "x86_64-darwin" ];
