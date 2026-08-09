{
  greeting ? "Hello",
}:
with import ../config.nix;
mkDerivation {
  name = "greet";
  buildCommand = ''
    mkdir -p $out/bin
    echo "#! ${shell}" > $out/bin/greet
    echo "echo ${greeting} World" >> $out/bin/greet
    chmod +x $out/bin/greet
  '';
}
