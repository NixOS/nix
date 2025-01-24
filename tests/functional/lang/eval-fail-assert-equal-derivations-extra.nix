assert
  {
    foo = {
      type = "derivation";
      outPath = "/nix/store/0";
    };
  } == {
    foo = {
      type = "derivation";
      outPath = "/nix/store/1";
      devious = true;
    };
  };
throw "unreachable"
