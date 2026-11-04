{
  mkFuzzSeedCheck,
  package,
}:

mkFuzzSeedCheck {
  inherit package;
  targets =
    (map
      (name: {
        inherit name;
        corpus = ./data/nars;
        dictionary = ./data/nars.dict;
      })
      [
        "fuzz-parse-dump"
        "fuzz-parse-dump-case-hacked"
      ]
    )
    ++ [
      {
        name = "fuzz-handle-json-message";
        corpus = ./data/handle-json-message;
      }
      {
        name = "fuzz-filter-ansi";
        # TODO: Extend the corpus. Though the functions are simple enough
        # to achieve full coverage without a good initial set.
        corpus = ./data/filter-ansi;
      }
    ];
}
