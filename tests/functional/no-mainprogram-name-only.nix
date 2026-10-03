# A package with only 'name' (no pname, no meta.mainProgram), and only has a
# binary named differently. Used to test the 'name' provenance case.
with import ./config.nix;

mkDerivation {
  name = "mytool-2.0";
  outputs = [ "out" ];
  buildCommand = ''
    mkdir -p $out/bin
    cat > $out/bin/helper <<EOF
    #! ${shell}
    echo "only helper exists, not mytool"
    EOF
    chmod +x $out/bin/helper
  '';
}
