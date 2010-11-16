let

  overrides = { a = 2; };

in (rec {
  __overrides = overrides;
  x = a;
  a = 1;
}).x
