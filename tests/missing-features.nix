{
  zero = derivation {
    name = "testing-absent-features";
    system = "x86_64-bogus";
    builder = "/bogus";
    args = [];

    foo = builtins.currentTime;

    meta_description = "Testing multiple absent features";
  };

  one = derivation {
    name = "testing-absent-features";
    system = "x86_64-bogus";
    builder = "/bogus";
    args = [];

    foo = builtins.currentTime;

    meta_description = "Testing multiple absent features";
    requiredSystemFeatures = [ "bogus" ];
  };

  multiple = derivation {
    name = "testing-absent-features";
    system = "x86_64-bogus";
    builder = "/bogus";
    args = [];

    foo = builtins.currentTime;

    meta_description = "Testing multiple absent features";
    requiredSystemFeatures = [ "bogus" "feature" ];
  };
}
