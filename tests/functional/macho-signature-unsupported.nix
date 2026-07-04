with import ./config.nix;

# Like macho-signature-check.nix, but the binary's CodeDirectory is
# patched to declare an unsupported hash type (SHA-384-shaped). The
# rewrite guard's detection still classifies the file as repairable —
# it only looks for the signature, not at the CodeDirectory — but the
# repair tool skips what it cannot process and exits successfully.
# Only the re-check after the repair catches that nothing was
# verified; without it the build would register the broken binary.

mkDerivation {
  name = "macho-signature-unsupported";
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
    /usr/bin/cc -O0 -Wl,-adhoc_codesign -DDOC_PATH="\"$docPlaceholder\"" -o hello main.c

    # Overwrite every CodeDirectory's hashType with 3 (SHA-384, which
    # the repair tool does not support).
    /usr/bin/python3 - hello <<'PY'
    import struct, sys
    path = sys.argv[1]
    b = bytearray(open(path, "rb").read())
    LC_CODE_SIGNATURE = 0x1d
    def be32(o): return (b[o] << 24) | (b[o+1] << 16) | (b[o+2] << 8) | b[o+3]
    def le32(o): return struct.unpack("<I", bytes(b[o:o+4]))[0]
    ncmds, off = le32(16), 32
    sig_off = None
    for _ in range(ncmds):
        cmd, sz = le32(off), le32(off + 4)
        if cmd == LC_CODE_SIGNATURE:
            sig_off = le32(off + 8)
        off += sz
    assert sig_off is not None
    assert be32(sig_off) == 0xfade0cc0
    patched = 0
    for i in range(be32(sig_off + 8)):
        rel = be32(sig_off + 12 + i * 8 + 4)
        if be32(sig_off + rel) == 0xfade0c02:
            b[sig_off + rel + 37] = 3
            patched += 1
    assert patched > 0
    open(path, "wb").write(b)
    PY

    mkdir -p "$out/bin" "$doc/share/doc"
    cp hello "$out/bin/hello"
    echo "hello docs" > "$doc/share/doc/hello"
  '';
}
