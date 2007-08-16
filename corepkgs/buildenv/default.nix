{system, derivations, stateIdentifiers, manifest, nixBinDir, nixStore}:

derivation { 
  name = "user-environment";
  system = system;
  builder = ./builder.pl;
  derivations = derivations;
  stateIdentifiers = stateIdentifiers;
  manifest = manifest;
  inherit nixBinDir nixStore;
}
