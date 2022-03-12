builtins.toJSON
  { a = 123;
    b = -456;
    c = "foo";
    d = "foo\n\"bar\"";
    e = true;
    f = false;
    g = [ 1 2 3 ];
    h = [ "a" [ "b" { "foo\nbar" = {}; } ] ];
    i = 1 + 2;
    j = 1.44;
    k = { __toString = self: self.a; a = "foo"; };
    l = 1.1e20;
    m = 2.3e-10;
    n = 1.00000000001;
  }
