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
        ab=$a/opt/from-a-to-b.txt
        bc=$b/opt/from-b-to-c.txt
        ca=$c/opt/from-c-to-a.txt
        echo $bc > $ab
        echo $ca > $bc
        echo $ab > $ca
        echo "common prefix path" >$a/opt/from
        echo "common prefix path" >$b/opt/from
        echo "common prefix path" >$c/opt/from

        # a c b a
        ac=$a/opt/from-a-to-c.2.txt
        cb=$c/opt/from-c-to-b.2.txt
        ba=$b/opt/from-b-to-a.2.txt
        echo $cb > $ac
        echo $ba > $cb
        echo $ac > $ba
      '';
  }).a;

  e = mkDerivation {
    name = "multiple-outputs-e";
    outputs = [ "a" "b" "c" ];
    meta.outputsToInstall = [ "a" "b" ];
    buildCommand = "mkdir $a $b $c";
  };

}
