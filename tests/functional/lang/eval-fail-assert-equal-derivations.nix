assert
  {
    foo = {
      type = "derivation";
      outPath = "/nix/store/0";
      ignored = abort "not ignored";
    };
  } == {
    foo = {
      type = "derivation";
      outPath = "/nix/store/1";
      ignored = abort "not ignored";
    };
  };
throw "unreachable"
