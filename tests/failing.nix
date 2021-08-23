{ busybox }:
with import ./config.nix;
let

  mkDerivation = args:
    derivation ({
      inherit system;
      builder = busybox;
      args = ["sh" "-e" args.builder or (builtins.toFile "builder-${args.name}.sh" "if [ -e .attrs.sh ]; then source .attrs.sh; fi; eval \"$buildCommand\"")];
    } // removeAttrs args ["builder" "meta"])
    // { meta = args.meta or {}; };
in
{

  failing = mkDerivation {
    name = "failing";
    buildCommand = ''
      echo foo > bar
      exit 1
    '';
  };
}
