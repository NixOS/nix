# dynamic attrs are not generally allowed in `let`, and inherit, but they are if they only contain a string
let
  ${"a"} = 1;
  attrs = rec {
    b = c;
    ${"c"} = d;
    d = a;
  };
in
{
  inherit ${"a"};
  inherit attrs;
  inherit (attrs) ${"b"} ${"c"} d;
}
