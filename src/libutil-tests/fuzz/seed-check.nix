{
  mkFuzzSeedCheck,
  package,
}:

mkFuzzSeedCheck {
  inherit package;
  targets =
    map
      (name: {
        inherit name;
        corpus = ./data/nars;
        dictionary = ./data/nars.dict;
      })
      [
        "fuzz-parse-dump"
        "fuzz-parse-dump-case-hacked"
      ];
}
