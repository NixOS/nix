with import ./config.nix;

let

  a = mkDerivation {
    name = "multiple-outputs-a";
    outputs = [ "first" "second" ];
    builder = ./multiple-outputs.a.builder.sh;
    helloString = "Hello, world!";
  };

in

assert a.second.helloString == "Hello, world!";

mkDerivation {
  defaultOutput = a;
  firstOutput = a.first.first;
  secondOutput = a.second.first.first.second.second.first.second;
  allOutputs = a.all;
  name = "multiple-outputs-b";
  builder = ./multiple-outputs.b.builder.sh;
}
