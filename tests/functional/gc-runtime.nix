with import ./config.nix;

{
  environ = mkDerivation {
    name = "gc-runtime-environ";
    buildCommand = "mkdir $out; echo environ > $out/environ";
  };

  open = mkDerivation {
    name = "gc-runtime-open";
    buildCommand = "mkdir $out; echo open > $out/open";
  };

  program = mkDerivation {
    name = "gc-runtime-program";
    builder =
      # Test inline source file definitions.
      builtins.toFile "builder.sh" ''
        mkdir $out

        cat > $out/program << 'EOF'
        #! ${shell}
        sleep 10000 < "$1"
        EOF

        chmod +x $out/program
      '';
  };
}
