with import <nix/config.nix>;

{ system, derivations, manifest }:

derivation { 
  name = "user-environment";
  system = system;
  builder = perl;
  args = [ "-w" ./buildenv.pl ];
  
  manifest = manifest;

  # !!! grmbl, need structured data for passing this in a clean way.
  paths = derivations;
  active = map (x: if x ? meta && x.meta ? active then x.meta.active else "true") derivations;
  priority = map (x: if x ? meta && x.meta ? priority then x.meta.priority else "5") derivations;

  # Building user environments remotely just causes huge amounts of
  # network traffic, so don't do that.
  preferLocalBuild = true;
}
