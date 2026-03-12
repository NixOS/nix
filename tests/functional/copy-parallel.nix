with import ./config.nix;
let
  mk =
    name: dep:
    mkDerivation {
      inherit name;
      buildCommand = ''
        mkdir $out
        ${if dep == null then "" else "echo ${dep} > $out/dep"}
        echo ${name} > $out/name
      '';
    };
  p1 = mk "chain-1" null;
  p2 = mk "chain-2" p1;
  p3 = mk "chain-3" p2;
  p4 = mk "chain-4" p3;
  p5 = mk "chain-5" p4;
  p6 = mk "chain-6" p5;
  p7 = mk "chain-7" p6;
  p8 = mk "chain-8" p7;
in
p8
