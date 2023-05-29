# Hack to get nan, inf/-inf values
let
  values = builtins.fromTOML "nan = nan\ninf = inf";
  inherit (values) nan inf;
in
builtins.toTOML
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
    k = nan;
    l = -nan;
    m = inf;
    n = -inf;
    o = { __toString = self: self.a; a = "foo"; };
    p = [
      {}
      { outPath = "foobar"; a = { b = builtins.abort "unreachable"; }; }
      { x = 1; y = 0.0; z = ""; }
    ];
  }
