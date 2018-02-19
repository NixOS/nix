with builtins;

[ (replaceStrings ["o"] ["a"] "foobar")
  (replaceStrings ["o"] [""] "foobar")
  (replaceStrings ["oo"] ["u"] "foobar")
  (replaceStrings ["oo" "a"] ["a" "oo"] "foobar")
  (replaceStrings ["oo" "oo"] ["u" "i"] "foobar")
  (replaceStrings [""] ["X"] "abc")
  (replaceStrings [""] ["X"] "")
  (replaceStrings ["-"] ["_"] "a-b")
]
