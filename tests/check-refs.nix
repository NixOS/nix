with import ./config.nix;

rec {

  dep = import ./dependencies.nix;

  makeTest = nr: args: mkDerivation ({
    name = "check-refs-" + toString nr;
  } // args);

  src = builtins.toFile "aux-ref" "bla bla";

  test1 = makeTest 1 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    inherit dep;
  };

  test2 = makeTest 2 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s ${src} $out/link";
    inherit dep;
  };

  test3 = makeTest 3 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    allowedReferences = [];
    inherit dep;
  };

  test4 = makeTest 4 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    allowedReferences = [dep];
    inherit dep;
  };

  test5 = makeTest 5 {
    builder = builtins.toFile "builder.sh" "mkdir $out";
    allowedReferences = [];
    inherit dep;
  };

  test6 = makeTest 6 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $out $out/link";
    allowedReferences = [];
    inherit dep;
  };

  test7 = makeTest 7 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $out $out/link";
    allowedReferences = ["out"];
    inherit dep;
  };

  test8 = makeTest 8 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s ${test1} $out/link";
    inherit dep;
  };

}
