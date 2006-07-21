let {

  input1 = derivation {
    name = "dependencies-input-1";
    system = "i086-msdos";
    builder = "/bar/sh";
    args = ["-e" "-x" ./dummy];
  };

  input2 = derivation {
    name = "dependencies-input-2";
    system = "i086-msdos";
    builder = "/bar/sh";
    args = ["-e" "-x" ./dummy];
    outputHashMode = "recursive";
    outputHashAlgo = "md5";
    outputHash = "ffffffffffffffffffffffffffffffff";
  };

  body = derivation {
    name = "dependencies";
    system = "i086-msdos";
    builder = "/bar/sh";
    args = ["-e" "-x" (./dummy  + "/FOOBAR/../.")];
    input1 = input1 + "/.";
    inherit input2;
  };

}