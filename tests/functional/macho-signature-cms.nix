with import ./config.nix;

# Like macho-signature-check.nix, but the binary's SuperBlob carries a
# synthetic non-empty CMS blob wrapper (the shape of a Developer-ID
# signature). Repair must never touch such a file — recomputing the
# page hashes would invalidate the (would-be) PKCS#7 chain — so the
# rewrite guard refuses even with the signature repair hook enabled, and
# the at-rest sweep skips the path.

mkDerivation {
  name = "macho-signature-cms";
  outputs = [
    "out"
    "doc"
  ];
  docPlaceholder = "${builtins.placeholder "doc"}/share/doc/hello";
  buildCommand = ''
    # Padded source widens the CodeDirectory (more code pages → more
    # hash slots) so the default ad-hoc signature has enough slack to
    # accept a small synthetic CMS blob in place.
    cat > main.c <<'EOF'
    #include <stdio.h>
    static const volatile char padding[131072] = { 0 };
    int main(void) {
        printf("doc=%s\n", DOC_PATH);
        (void) padding;
        return 0;
    }
    EOF
    /usr/bin/cc -O0 -Wl,-adhoc_codesign -DDOC_PATH="\"$docPlaceholder\"" -o hello-cms main.c

    # Inject a non-empty CSMAGIC_BLOBWRAPPER under CSSLOT_SIGNATURESLOT
    # into the SuperBlob, rebuilding the blob index contiguously and
    # growing the __LINKEDIT segment to fit.
    /usr/bin/python3 - hello-cms <<'PY'
    import struct, sys
    path = sys.argv[1]
    b = bytearray(open(path, "rb").read())
    LC_SEGMENT_64 = 0x19
    LC_CODE_SIGNATURE = 0x1d
    def be32(o): return (b[o] << 24) | (b[o+1] << 16) | (b[o+2] << 8) | b[o+3]
    def le32(o): return struct.unpack("<I", bytes(b[o:o+4]))[0]
    def le64(o): return struct.unpack("<Q", bytes(b[o:o+8]))[0]
    ncmds, off = le32(16), 32
    lc_sig_off = sig_off = sig_sz = None
    le_filesize_off = None
    for _ in range(ncmds):
        cmd, sz = le32(off), le32(off + 4)
        if cmd == LC_CODE_SIGNATURE:
            lc_sig_off = off
            sig_off, sig_sz = le32(off + 8), le32(off + 12)
        elif cmd == LC_SEGMENT_64:
            if bytes(b[off + 8 : off + 24]).rstrip(b"\x00") == b"__LINKEDIT":
                le_filesize_off = off + 48
        off += sz
    assert sig_off is not None and le_filesize_off is not None
    assert be32(sig_off) == 0xfade0cc0
    sb_count = be32(sig_off + 8)
    entries = []
    for i in range(sb_count):
        eo = sig_off + 12 + i * 8
        typ, rel = be32(eo), be32(eo + 4)
        blen = be32(sig_off + rel + 4)
        entries.append((typ, bytes(b[sig_off + rel : sig_off + rel + blen])))
    entries.append((0x10000, struct.pack(">II", 0xfade0b01, 12) + bytes(4)))
    entries_sz = len(entries) * 8
    cursor = 12 + entries_sz
    new_entries, new_blobs = [], bytearray()
    for typ, blob in entries:
        new_entries.append((typ, cursor)); new_blobs += blob; cursor += len(blob)
    new_sb_length = cursor
    new_sig_sz = (new_sb_length + 15) & ~15
    if new_sig_sz > sig_sz:
        growth = new_sig_sz - sig_sz
        b.extend(bytes(growth))
        struct.pack_into("<I", b, lc_sig_off + 12, new_sig_sz)
        struct.pack_into("<Q", b, le_filesize_off, le64(le_filesize_off) + growth)
        sig_sz = new_sig_sz
    region = bytearray(sig_sz)
    struct.pack_into(">III", region, 0, 0xfade0cc0, new_sb_length, len(entries))
    for i, (typ, rel) in enumerate(new_entries):
        struct.pack_into(">II", region, 12 + i * 8, typ, rel)
    region[12 + entries_sz : 12 + entries_sz + len(new_blobs)] = new_blobs
    b[sig_off : sig_off + sig_sz] = region
    open(path, "wb").write(b)
    PY

    mkdir -p "$out/bin" "$doc/share/doc"
    cp hello-cms "$out/bin/hello-cms"
    echo "hello docs" > "$doc/share/doc/hello"
  '';
}
