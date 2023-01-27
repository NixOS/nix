with import ./config.nix;

let

  input = import ./simple.nix;

  dependent = mkDerivation {
    name = "dependent";
    buildCommand = ''
      mkdir -p $out
      echo -n "$input1" > "$out/file1"
      echo -n "$input2" > "$out/file2"
    '';
    input1 = "${input}/hello";
    input2 = "hello";
  };

  readDependent = mkDerivation {
    # Will evaluate correctly because file2 doesn't have any references,
    # even though the `dependent` derivation does.
    name = builtins.readFile (dependent + "/file2");
    buildCommand = ''
      echo "$input" > "$out"
    '';
    input = builtins.readFile (dependent + "/file1");
  };

in readDependent
