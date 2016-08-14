with import <nix/config.nix>;

{ derivations, manifest }:

derivation {
  name = "user-environment";
  system = builtins.currentSystem;
  builder = nixLibexecDir + "/nix/buildenv";

  inherit manifest;

  # !!! grmbl, need structured data for passing this in a clean way.
  derivations =
    map (d:
      [ (d.meta.active or "true")
        (d.meta.priority or 5)
        (builtins.length d.outputs)
      ] ++ map (output: builtins.getAttr output d) d.outputs)
      derivations;

  # Building user environments remotely just causes huge amounts of
  # network traffic, so don't do that.
  preferLocalBuild = true;

  # Also don't bother substituting.
  allowSubstitutes = false;

  __impureHostDeps = [
    "/usr/lib/libSystem.dylib"
  ];

  __sandboxProfile = ''
    (allow sysctl-read)
    (allow file-read*
           (literal "/etc")
           (literal "/private/etc")
           (subpath "/private/etc/nix"))
  '';

  inherit chrootDeps;
}
