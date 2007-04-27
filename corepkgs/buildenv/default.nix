{system, derivations, manifest}:

derivation { 
  name = "user-environment";
  system = system;
  builder = ./builder.pl;
  derivations = derivations;
  manifest = manifest;

  # !!! grmbl, need structured data for passing this in a clean way.
  active = map (x: if x ? meta && x.meta ? active then x.meta.active else "true") derivations;
}
