{system, derivations, manifest}:

derivation { 
  name = "user-environment";
  system = system;
  builder = ./builder.pl;
  derivations = derivations;
  manifest = manifest;
}
