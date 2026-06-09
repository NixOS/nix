with import ../config.nix;
mkDerivation {
  name = "meta-types-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    # Proper types
    anInt = 42;
    aBool = true;
    aFloat = 3.14;
    aString = "hello";
    aList = [
      1
      2
      3
    ];
    # String-encoded values (backwards compatibility)
    stringInt = "123";
    stringBool = "true";
    stringFloat = "2.72";
  };
}
