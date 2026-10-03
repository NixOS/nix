# A package that has pname but no meta.mainProgram, and only has a binary
# named differently (bin/helper instead of bin/multi-tool). Used to test that
# "nix run" shows a helpful error when the inferred binary doesn't exist.
with import ./config.nix;

mkDerivation {
  pname = "multi-tool";
  version = "1.0";
  name = "multi-tool-1.0";
  outputs = [ "out" ];
  buildCommand = ''
    mkdir -p $out/bin
    cat > $out/bin/helper <<EOF
    #! ${shell}
    echo "only helper exists, not multi-tool"
    EOF
    chmod +x $out/bin/helper
  '';
}
