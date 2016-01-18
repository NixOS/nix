with import <nix/config.nix>;

{ derivations, manifest }:

derivation {
  name = "user-environment";
  system = builtins.currentSystem;
  builder = perl;
  args = [ "-w" ./buildenv.pl ];

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

  __sandboxProfile = ''
    (allow sysctl-read)
    (allow file-read*
           (literal "/usr/lib/libSystem.dylib")
           (literal "/usr/lib/libSystem.B.dylib")
           (literal "/usr/lib/libobjc.A.dylib")
           (literal "/usr/lib/libobjc.dylib")
           (literal "/usr/lib/libauto.dylib")
           (literal "/usr/lib/libc++abi.dylib")
           (literal "/usr/lib/libc++.1.dylib")
           (literal "/usr/lib/libDiagnosticMessagesClient.dylib")
           (subpath "/usr/lib/system")
           (subpath "/dev"))
  '';

  inherit chrootDeps;
}
