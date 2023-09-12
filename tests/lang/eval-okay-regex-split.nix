with builtins;

# Non capturing regex returns empty lists
assert  split "foobar" "foobar"  == ["" [] ""];
assert  split "fo*" "f"          == ["" [] ""];
assert  split "fo+" "f"          == ["f"];
assert  split "fo*" "fo"         == ["" [] ""];
assert  split "fo*" "foo"        == ["" [] ""];
assert  split "fo+" "foo"        == ["" [] ""];
assert  split "fo{1,2}" "foo"    == ["" [] ""];
assert  split "fo{1,2}" "fooo"   == ["" [] "o"];
assert  split "fo*" "foobar"     == ["" [] "bar"];

# Capturing regex returns a list of sub-matches
assert  split "(fo*)" "f"        == ["" ["f"] ""];
assert  split "(fo+)" "f"        == ["f"];
assert  split "(fo*)" "fo"       == ["" ["fo"] ""];
assert  split "(f)(o*)" "f"      == ["" ["f" ""] ""];
assert  split "(f)(o*)" "foo"    == ["" ["f" "oo"] ""];
assert  split "(fo+)" "foo"      == ["" ["foo"] ""];
assert  split "(fo{1,2})" "foo"  == ["" ["foo"] ""];
assert  split "(fo{1,2})" "fooo" == ["" ["foo"] "o"];
assert  split "(fo*)" "foobar"   == ["" ["foo"] "bar"];

# Matches are greedy.
assert  split "(o+)" "oooofoooo" == ["" ["oooo"] "f" ["oooo"] ""];

# Matches multiple times.
assert  split "(b)" "foobarbaz"  == ["foo" ["b"] "ar" ["b"] "az"];

# Split large strings containing newlines. null are inserted when a
# pattern within the current did not match anything.
assert  split "[[:space:]]+|([',.!?])" ''
  Nix Rocks!
  That's why I use it.
''  == [
  "Nix" [ null ] "Rocks" ["!"] "" [ null ]
  "That" ["'"] "s" [ null ] "why" [ null ] "I" [ null ] "use" [ null ] "it" ["."] "" [ null ]
  ""
];

# Documentation examples
assert  split  "(a)b" "abc"      == [ "" [ "a" ] "c" ];
assert  split  "([ac])" "abc"    == [ "" [ "a" ] "b" [ "c" ] "" ];
assert  split  "(a)|(c)" "abc"   == [ "" [ "a" null ] "b" [ null "c" ] "" ];
assert  split  "([[:upper:]]+)" "  FOO   " == [ "  " [ "FOO" ] "   " ];

true
