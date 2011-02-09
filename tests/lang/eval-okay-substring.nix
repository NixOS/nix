with builtins;

let

  s = "foobar";

in

substring 1 2 s
+ "x"
+ substring 0 (stringLength s) s
+ "y"
+ substring 3 100 s
+ "z"
+ substring 2 (sub (stringLength s) 3) s
+ "a"
+ substring 3 0 s
+ "b"
+ substring 3 1 s
+ "c"
+ substring 5 10 "perl"
