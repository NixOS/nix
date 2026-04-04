{
  busybox,
}:

with import ./config.nix;

let
  mkDerivation =
    args:
    derivation (
      {
        inherit system;
        builder = busybox;
        args = [
          "sh"
          "-e"
          args.builder or (builtins.toFile "builder-${args.name}.sh" ''
            if [ -e "$NIX_ATTRS_SH_FILE" ]; then source $NIX_ATTRS_SH_FILE; fi;
            eval "$buildCommand"
          '')
        ];
      }
      // removeAttrs args [
        "builder"
        "meta"
        "passthru"
      ]
    )
    // {
      meta = args.meta or { };
      passthru = args.passthru or { };
    };

  # A derivation whose builder emits @nix structured log messages,
  # including setPhase. This lets us test that these messages are
  # properly forwarded from remote builds.
  phased = mkDerivation {
    shell = busybox;
    name = "build-remote-logging-phased";
    buildCommand = ''
      echo '@nix {"action":"setPhase","phase":"unpackPhase"}'
      echo 'unpacking...'
      echo '@nix {"action":"setPhase","phase":"buildPhase"}'
      echo 'building...'
      echo '@nix {"action":"setPhase","phase":"installPhase"}'
      echo 'done' > $out
    '';
    requiredSystemFeatures = [ "logging-test" ];
  };

in
phased
