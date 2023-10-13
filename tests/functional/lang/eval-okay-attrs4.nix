let

  as = { x.y.z = 123; a.b.c = 456; };

  bs = null;

in [ (as ? x) (as ? y) (as ? x.y.z) (as ? x.y.z.a) (as ? x.y.a) (as ? a.b.c) (bs ? x) (bs ? x.y.z) ]
