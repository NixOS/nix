with import ./lib.nix;

let

  as = { x.y.z = 123; a.b.c = 456; };

  bs = { f-o-o.bar = "foo"; };

  or = x: y: x || y;
  
in
  [ as.x.y.z
    as.foo or "foo"
    as.x.y.bla or as.a.b.c
    as.a.b.c or as.x.y.z
    as.x.y.bla or bs.f-o-o.bar or "xyzzy"
    as.x.y.bla or bs.bar.foo or "xyzzy"
    (123).bla or null.foo or "xyzzy"
    # Backwards compatibility test.
    (fold or [] [true false false])
  ]
