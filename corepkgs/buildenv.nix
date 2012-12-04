with import <nix/config.nix>;

{ derivations, manifest }:

derivation {
  name = "user-environment";
  system = builtins.currentSystem;
  builder = perl;
  args = [ "-w" ./buildenv.pl ];

  manifest = manifest;

  # !!! grmbl, need structured data for passing this in a clean way.
  derivations =
    map (d:
      [ (if d.meta.active or true then "1" else "0")
        (d.meta.priority or 5)
        (builtins.length d.outputs)
      ] ++ map (output: builtins.getAttr output d) d.outputs)
      derivations;

  # Building user environments remotely just causes huge amounts of
  # network traffic, so don't do that.
  preferLocalBuild = true;

  # Don't build in a chroot because Nix's dependencies may not be there.
  __noChroot = true;
}
