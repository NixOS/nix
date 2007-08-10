{system, derivations, stateIdentifiers, manifest}:

derivation { 
  name = "user-environment";
  system = system;
  builder = ./builder.pl;
  derivations = derivations;
  stateIdentifiers = stateIdentifiers;
  manifest = manifest;
}
