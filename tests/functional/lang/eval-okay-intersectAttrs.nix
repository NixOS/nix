let
  alphabet =
  { a = "a";
    b = "b";
    c = "c";
    d = "d";
    e = "e";
    f = "f";
    g = "g";
    h = "h";
    i = "i";
    j = "j";
    k = "k";
    l = "l";
    m = "m";
    n = "n";
    o = "o";
    p = "p";
    q = "q";
    r = "r";
    s = "s";
    t = "t";
    u = "u";
    v = "v";
    w = "w";
    x = "x";
    y = "y";
    z = "z";
  };
  foo = {
    inherit (alphabet) f o b a r z q u x;
    aa = throw "aa";
  };
  alphabetFail = builtins.mapAttrs throw alphabet;
in
[ (builtins.intersectAttrs { a = abort "l1"; } { b = abort "r1"; })
  (builtins.intersectAttrs { a = abort "l2"; } { a = 1; })
  (builtins.intersectAttrs alphabetFail { a = 1; })
  (builtins.intersectAttrs  { a = abort "laa"; } alphabet)
  (builtins.intersectAttrs alphabetFail { m = 1; })
  (builtins.intersectAttrs  { m = abort "lam"; } alphabet)
  (builtins.intersectAttrs alphabetFail { n = 1; })
  (builtins.intersectAttrs  { n = abort "lan"; } alphabet)
  (builtins.intersectAttrs alphabetFail { n = 1; p = 2; })
  (builtins.intersectAttrs  { n = abort "lan2"; p = abort "lap"; } alphabet)
  (builtins.intersectAttrs alphabetFail { n = 1; p = 2; })
  (builtins.intersectAttrs  { n = abort "lan2"; p = abort "lap"; } alphabet)
  (builtins.intersectAttrs alphabetFail alphabet)
  (builtins.intersectAttrs alphabet foo == builtins.intersectAttrs foo alphabet)
]
