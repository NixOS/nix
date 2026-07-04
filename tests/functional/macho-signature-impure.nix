with import ./config.nix;

# Multi-output impure derivation whose `dev` output contains a signed
# binary embedding the `out` output's path. Impure outputs are moved
# into a daemon-private temporary directory before registration; the
# cross-output reference forces a hash rewrite there, so the repair
# hook must operate inside that directory (the chown-the-parent
# branch of the hook invocation).

mkDerivation {
  name = "macho-signature-impure";
  __impure = true;
  outputs = [
    "out"
    "dev"
  ];
  buildCommand = ''
    mkdir -p "$out" "$dev/bin"
    echo data > "$out/data"

    cat > main.c <<EOF
    #include <stdio.h>
    int main(void) {
        printf("out=%s\n", "$out/data");
        return 0;
    }
    EOF
    /usr/bin/cc -O0 -Wl,-adhoc_codesign -o "$dev/bin/hello" main.c
  '';
}
