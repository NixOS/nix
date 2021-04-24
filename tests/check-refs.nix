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

  test9 = makeTest 9 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    inherit dep;
    disallowedReferences = [dep];
  };

  test10 = makeTest 10 {
    builder = builtins.toFile "builder.sh" "mkdir $out; echo $test5; ln -s $dep $out/link";
    inherit dep test5;
    disallowedReferences = [test5];
  };

  test11 = makeTest 11 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two";
    outputs = ["out" "two"];
    inherit dep;
    allowedReferences = [];
  };

  test12 = makeTest 12 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $two";
    outputs = ["out" "two"];
    inherit dep;
    allowedReferences = [];
  };

  test13 = makeTest 13 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $two; ln -s $dep $out";
    outputs = ["out" "two"];
    inherit dep;
    allowedReferences = [dep];
  };

  test14 = makeTest 14 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $out";
    outputs = ["out" "two"];
    inherit dep;
    allowedReferences.out = [dep];
  };

  test15 = makeTest 15 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $out; ln -s $dep $two";
    outputs = ["out" "two"];
    inherit dep;
    allowedReferences.out = [dep];
  };

  test16 = makeTest 16 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $out; ln -s $dep $two";
    outputs = ["out" "two"];
    inherit dep;
    allowedReferences.out = [dep];
    allowedReferences.two = [dep];
  };

  test17 = makeTest 17 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $out; ln -s $dep $two";
    outputs = ["out" "two"];
    inherit dep;
    disallowedReferences.out = [];
    disallowedReferences.two = [];
  };

  test18 = makeTest 18 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $out; ln -s $dep $two";
    outputs = ["out" "two"];
    inherit dep;
    disallowedReferences.out = [dep];
  };

  test19 = makeTest 19 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $two";
    outputs = ["out" "two"];
    inherit dep;
    disallowedReferences.out = [dep];
  };

  test20 = makeTest 20 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two";
    outputs = ["out" "two"];
    inherit dep;
    disallowedReferences.out = [dep];
    disallowedReferences.two = [dep];
  };

  test21 = makeTest 21 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two";
    outputs = ["out" "two"];
    inherit dep;
    disallowedReferences.two = [dep "out"];
  };

  test22 = makeTest 22 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $out $two";
    outputs = ["out" "two"];
    inherit dep;
    disallowedReferences.two = [dep "out"];
  };

  test90 = makeTest 90 {
    builder = builtins.toFile "builder.sh" "mkdir $out; mkdir $two; ln -s $dep $out; ln -s $dep $two";
    outputs = ["out" "two"];
    inherit dep;
  };

}
