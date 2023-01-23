with import ./config.nix;

{ foo }:

{
  # These should be ignored by 'nix search -f'
  throws = throw "This fails to evaluate";
  randomFunction = name: "Hello, ${name}!";

  nestNoRecurse = {
    hidden = mkDerivation rec {
      name = "hidden-1";
      buildCommand = "echo ${name} > $out";
    };
  };

  nestRecurse = {
    recurseForDerivations = true;
    inner = mkDerivation rec {
      name = "inner-1";
      buildCommand = "echo ${name} > $out";
    };
  };
}
