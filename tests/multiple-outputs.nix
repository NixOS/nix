with import ./config.nix;

rec {

  # Want to ensure that "out" doesn't get a suffix on it's path.
  nameCheck = mkDerivation {
    name = "multiple-outputs-a";
    outputs = [ "out" "dev" ];
    builder = builtins.toFile "builder.sh"
      ''
        mkdir $first $second
        test -z $all
        echo "first" > $first/file
        echo "second" > $second/file
        ln -s $first $second/link
      '';
    helloString = "Hello, world!";
  };

  a = mkDerivation {
    name = "multiple-outputs-a";
    outputs = [ "first" "second" ];
    builder = builtins.toFile "builder.sh"
      ''
        mkdir $first $second
        test -z $all
        echo "first" > $first/file
        echo "second" > $second/file
        ln -s $first $second/link
      '';
    helloString = "Hello, world!";
  };

  b = mkDerivation {
    defaultOutput = assert a.second.helloString == "Hello, world!"; a;
    firstOutput = assert a.outputName == "first"; a.first.first;
    secondOutput = assert a.second.outputName == "second"; a.second.first.first.second.second.first.second;
    allOutputs = a.all;
    name = "multiple-outputs-b";
    builder = builtins.toFile "builder.sh"
      ''
        mkdir $out
        test "$firstOutput $secondOutput" = "$allOutputs"
        test "$defaultOutput" = "$firstOutput"
        test "$(cat $firstOutput/file)" = "first"
        test "$(cat $secondOutput/file)" = "second"
        echo "success" > $out/file
      '';
  };

  c = mkDerivation {
    name = "multiple-outputs-c";
    drv = b.drvPath;
    builder = builtins.toFile "builder.sh"
      ''
        mkdir $out
        ln -s $drv $out/drv
      '';
  };

  d = mkDerivation {
    name = "multiple-outputs-d";
    drv = builtins.unsafeDiscardOutputDependency b.drvPath;
    builder = builtins.toFile "builder.sh"
      ''
        mkdir $out
        echo $drv > $out/drv
      '';
  };

  # cycle: a -> c -> b -> a
  cyclic = (mkDerivation {
    name = "cyclic-outputs";
    outputs = [ "a" "b" "c" ];
    builder = builtins.toFile "builder.sh"
      ''
        mkdir -p $a/opt $b/opt $c/opt

        # a b c a
        ab=$a/a-to-b
        bc=$b/b-to-c
        ca=$c/c-to-a
        echo $bc > $ab
        echo $ca > $bc
        echo $ab > $ca

        # a c b a
        ac=$a/a-to-c.2
        cb=$c/c-to-b.2
        ba=$b/b-to-a.2
        echo $cb > $ac
        echo $ba > $cb
        echo $ac > $ba

        # a b c
        ab=$a/a-to-b.3
        bc=$b/b-to-c.3
        echo $bc > $ab
        echo ___ > $bc
      '';
  }).a;

  e = mkDerivation {
    name = "multiple-outputs-e";
    outputs = [ "a" "b" "c" ];
    meta.outputsToInstall = [ "a" "b" ];
    buildCommand = "mkdir $a $b $c";
  };

}
