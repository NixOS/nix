with import ./config.nix;

# Multi-output input-addressed derivation whose `out` binary embeds a
# placeholder for the `doc` output. After the builder exits, the daemon
# rewrites the placeholder bytes to the final `doc` hash inside
# `bin/hello`. Apple's `ld` ad-hoc-signed the binary at link time over the
# pre-rewrite bytes, so the rewrite invalidates one or more SHA-256 page
# hashes in `LC_CODE_SIGNATURE.CodeDirectory`. Without the page-hash
# fix-up in `DerivationBuilderImpl::registerOutputs`, the kernel SIGKILLs
# the binary at first page-in.

mkDerivation {
  name = "macho-rewrite-multi";
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
    /usr/bin/cc -O0 \
      -DDOC_PATH="\"$docPlaceholder\"" \
      -o hello main.c
    mkdir -p "$out/bin" "$doc/share/doc"
    cp hello "$out/bin/hello"
    echo "hello docs" > "$doc/share/doc/hello"
  '';
}
