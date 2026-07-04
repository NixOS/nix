with import ./config.nix;

# Multi-output input-addressed derivation whose binaries embed a
# placeholder for the `doc` output. When `doc` is already present in
# the store at build start, the daemon rewrites the placeholder bytes
# inside both binaries after the builder exits — invalidating the
# signed one's page hashes, which is exactly what
# `macho-signature-rewrite-check` guards against. The unsigned twin
# is the negative control: same embed, same rewrite, no signature,
# so the guard must leave it alone.

mkDerivation {
  name = "macho-signature-check";
  outputs = [
    "out"
    "doc"
  ];
  docPlaceholder = "${builtins.placeholder "doc"}/share/doc/hello";
  buildCommand = ''
    cat > main.c <<'EOF'
    #include <stdio.h>
    int main(void) {
        printf("doc=%s\n", DOC_PATH);
        return 0;
    }
    EOF
    # `-adhoc_codesign` makes `ld` sign unconditionally; Apple's
    # linker only signs by default when targeting arm64.
    /usr/bin/cc -O0 -Wl,-adhoc_codesign -DDOC_PATH="\"$docPlaceholder\"" -o hello main.c
    # Unsigned twin: same placeholder embed, no code signature.
    /usr/bin/cc -O0 -Wl,-no_adhoc_codesign -DDOC_PATH="\"$docPlaceholder\"" -o hello-unsigned main.c

    mkdir -p "$out/bin" "$doc/share/doc"
    cp hello "$out/bin/hello"
    cp hello-unsigned "$out/bin/hello-unsigned"
    echo "hello docs" > "$doc/share/doc/hello"
  '';
}
