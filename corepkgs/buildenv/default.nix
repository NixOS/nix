{system, derivations, stateIdentifiers, runtimeStateArgs, manifest, nixBinDir, nixStore}:

derivation { 
  name = "user-environment";
  system = system;
  builder = ./builder.pl;
  derivations = derivations;
  runtimeStateArgs_arg = runtimeStateArgs;
  stateIdentifiers = stateIdentifiers;
  manifest = manifest;
  inherit nixBinDir nixStore;
}
