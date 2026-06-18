{ zeroSelfReference }:

with import ./config.nix;

mkDerivation {
  name = "foo";
  buildCommand = ''
    mkdir $out
    ${
      if zeroSelfReference then
        ''
          {
            printf "hello $NIX_STORE/"
            head -c 32 /dev/zero
            printf -- '-foo\n'
          } > $out/self-ref
        ''
      else
        ''
          echo "hello $out" > $out/self-ref
        ''
    }
      echo "hello $out" > $out/always-self-ref
  '';
}
