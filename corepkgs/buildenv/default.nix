{system, derivations, stateIdentifiers, manifest, nixBinDir, nixStore}:

derivation { 
  name = "user-environment";
  system = system;
  builder = ./builder.pl;
  
  stateIdentifiers = stateIdentifiers;
  manifest = manifest;
  inherit nixBinDir nixStore;

  # !!! grmbl, need structured data for passing this in a clean way.
  paths = derivations;
  active = map (x: if x ? meta && x.meta ? active then x.meta.active else "true") derivations;
  priority = map (x: if x ? meta && x.meta ? priority then x.meta.priority else "5") derivations;
}
