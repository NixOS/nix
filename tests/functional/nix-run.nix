with import ./config.nix;

let
  # Build a trivial executable at `$out/bin/<binName>` that echoes a
  # recognisable line when run. This lets the test assert which binary
  # `nix-run` picked without depending on anything from nixpkgs.
  mkProg =
    {
      name,
      binName,
      message,
      extra ? { },
    }:
    mkDerivation (
      {
        inherit name;
        buildCommand = ''
          mkdir -p $out/bin
          {
            echo '#!${shell}'
            echo 'echo ${message}'
            echo 'for arg in "$@"; do echo "arg:$arg"; done'
          } > $out/bin/${binName}
          chmod +x $out/bin/${binName}
        '';
      }
      // extra
    );

in

rec {

  # `meta.mainProgram` wins over everything else.
  withMainProgram = mkProg {
    name = "ignored-name-1.0";
    binName = "picked-by-main-program";
    message = "mainProgram";
    extra.meta.mainProgram = "picked-by-main-program";
  };

  # No `meta.mainProgram`: fall back to `pname`.
  withPname = mkProg {
    name = "ignored-name-2.0";
    binName = "picked-by-pname";
    message = "pname";
    extra.pname = "picked-by-pname";
  };

  # Neither `meta.mainProgram` nor `pname`: fall back to the parsed
  # derivation name.
  withParsedName = mkProg {
    name = "picked-by-parsed-name-3.0";
    binName = "picked-by-parsed-name";
    message = "parsedName";
  };

  # For the `-E` / expression-mode test: a single top-level derivation.
  topLevel = withMainProgram;

  # For the program-argument-forwarding test.
  echoArgs = mkProg {
    name = "echo-args-1.0";
    binName = "echo-args";
    message = "echoArgs";
    extra.meta.mainProgram = "echo-args";
  };

}
