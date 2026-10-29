{
  mkFuzzSeedCheck,
  package,
}:

mkFuzzSeedCheck {
  inherit package;
  targets = [
    {
      name = "fuzz-parse-derivation";
      corpus = ./data/derivations;
      dictionary = ./data/derivations.dict;
    }
    {
      name = "fuzz-parse-derivation-experimental";
      corpus = ./data/derivations;
      dictionary = ./data/derivations.dict;
    }
    {
      name = "fuzz-store-path";
      corpus = ./data/store-paths;
    }
  ];
}
