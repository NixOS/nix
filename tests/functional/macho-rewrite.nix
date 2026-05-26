with import ./config.nix;

# Multi-output input-addressed derivation whose binaries each embed
# a placeholder for the `doc` output. After the builder exits, the
# daemon rewrites the placeholder bytes inside each signed Mach-O —
# invalidating the page hashes until the fix-up runs.
#
# Coverage: thin + 1-arch fat32 + multi-arch fat32 + fat64 dylib
# plus two malformed fat-header fixtures and a symlink.

mkDerivation {
  name = "macho-rewrite-multi";
  outputs = [
    "out"
    "doc"
  ];
  docPlaceholder = "${builtins.placeholder "doc"}/share/doc/hello";
  outPlaceholder = "${builtins.placeholder "out"}/bin/hello-self";
  buildCommand = ''
    cat > main.c <<'EOF'
    #include <stdio.h>
    int main(void) {
        printf("doc=%s\n", DOC_PATH);
        return 0;
    }
    EOF

    /usr/bin/cc -O0 -DDOC_PATH="\"$docPlaceholder\"" -o hello main.c

    # Self-reference trigger variant: binary embeds its own $out path.
    # When the output is already in the store at the start of a
    # rebuild (--check, or a CA cold build), `RewritingSink`
    # substitutes the scratch path for the final hash inside these
    # bytes, same as the sibling-reference case.
    cat > self.c <<'EOF'
    #include <stdio.h>
    int main(void) {
        printf("self=%s\n", OUT_PATH);
        return 0;
    }
    EOF
    /usr/bin/cc -O0 -DOUT_PATH="\"$outPlaceholder\"" -o hello-self self.c

    # codesign-signed variant: re-sign after `ld` to clear
    # `linker-signed` and switch to 16 KiB pages. The helper respects
    # the CD's pageSize, so the rewrite path differs from the
    # linker-signed fixtures.
    /usr/bin/cc -O0 -DDOC_PATH="\"$docPlaceholder\"" -o hello-codesigned main.c
    /usr/bin/codesign -f -s - hello-codesigned

    # `-Wl,-adhoc_codesign` forces `ld` to ad-hoc-sign the cross-arch
    # slice too — Apple's linker only signs the native arch by default,
    # which would leave the other slice unsigned and fail verify.
    for arch in arm64 x86_64; do
      /usr/bin/cc -O0 -arch "$arch" -Wl,-adhoc_codesign \
        -DDOC_PATH="\"$docPlaceholder\"" \
        -o "hello-$arch" main.c
    done

    case "$system" in
      aarch64-darwin) host_arch=arm64 ;;
      x86_64-darwin)  host_arch=x86_64 ;;
    esac

    /usr/bin/lipo -create "hello-$host_arch" -output hello-fat32-1arch
    /usr/bin/lipo -create hello-arm64 hello-x86_64 -output hello-fat32-multi

    # CMS-signed variant: inject a non-empty `CSMAGIC_BLOBWRAPPER`
    # into the SuperBlob under `CS_SIGNATURESLOT` (0x10000). The
    # helper's pre-scan must skip the slice — recomputing page
    # hashes would change the CodeDirectory and invalidate the
    # (synthetic) PKCS#7 chain that the wrapper represents. The
    # resulting binary keeps its stale page hashes after
    # `RewritingSink`, which the test asserts via `codesign --verify`.
    # Padded source widens the CodeDirectory (more code pages → more
    # hash slots) so the default ad-hoc signature has enough slack
    # to accept a 100-byte synthetic CMS blob in place.
    cat > main-padded.c <<'EOF'
    #include <stdio.h>
    static const volatile char padding[131072] = { 0 };
    int main(void) {
        printf("doc=%s\n", DOC_PATH);
        (void)padding;
        return 0;
    }
    EOF
    /usr/bin/cc -O0 -DDOC_PATH="\"$docPlaceholder\"" -o hello-cms main-padded.c
    /usr/bin/python3 - hello-cms <<'PY'
    import struct, sys
    path = sys.argv[1]
    b = bytearray(open(path, "rb").read())
    LC_SEGMENT_64 = 0x19
    LC_CODE_SIGNATURE = 0x1d
    def be32(o): return (b[o] << 24) | (b[o+1] << 16) | (b[o+2] << 8) | b[o+3]
    def le32(o): return struct.unpack("<I", bytes(b[o:o+4]))[0]
    def le64(o): return struct.unpack("<Q", bytes(b[o:o+8]))[0]
    # Walk load commands to find LC_CODE_SIGNATURE and __LINKEDIT segment.
    ncmds, off = le32(16), 32
    lc_sig_off = sig_off = sig_sz = None
    le_cmd_off = le_filesize_off = None
    for _ in range(ncmds):
        cmd, sz = le32(off), le32(off + 4)
        if cmd == LC_CODE_SIGNATURE:
            lc_sig_off = off
            sig_off, sig_sz = le32(off + 8), le32(off + 12)
        elif cmd == LC_SEGMENT_64:
            segname = bytes(b[off + 8 : off + 24]).rstrip(b"\x00")
            if segname == b"__LINKEDIT":
                le_cmd_off = off
                le_filesize_off = off + 48  # filesize is at offset 48 in segment_command_64
        off += sz
    assert sig_off is not None and le_cmd_off is not None, "missing LC_CODE_SIGNATURE or __LINKEDIT"
    # Parse existing SuperBlob and read its entries with their referenced blob bytes.
    assert be32(sig_off) == 0xfade0cc0, "not CSMAGIC_EMBEDDED_SIGNATURE"
    sb_count = be32(sig_off + 8)
    entries = []
    for i in range(sb_count):
        eo = sig_off + 12 + i * 8
        typ, rel = be32(eo), be32(eo + 4)
        blen = be32(sig_off + rel + 4)
        entries.append((typ, bytes(b[sig_off + rel : sig_off + rel + blen])))
    # Append a synthetic non-empty BlobWrap under CS_SIGNATURESLOT. Minimum
    # payload (blobLen > 8) is enough to trigger the helper's skip check;
    # 12 bytes keeps the growth small.
    cms = struct.pack(">II", 0xfade0b01, 12) + bytes(4)
    entries.append((0x10000, cms))
    # Repack contiguously at the start of the signature region.
    entries_sz = len(entries) * 8
    cursor = 12 + entries_sz
    new_entries, new_blobs = [], bytearray()
    for typ, blob in entries:
        new_entries.append((typ, cursor)); new_blobs += blob; cursor += len(blob)
    new_sb_length = cursor
    # Grow the signature region (and the __LINKEDIT segment that contains it)
    # enough to fit the rebuilt SuperBlob, rounded up to 16-byte alignment
    # to match how `ld` lays out signature blocks.
    new_sig_sz = (new_sb_length + 15) & ~15
    if new_sig_sz > sig_sz:
        growth = new_sig_sz - sig_sz
        b.extend(bytes(growth))
        struct.pack_into("<I", b, lc_sig_off + 12, new_sig_sz)
        cur_filesize = le64(le_filesize_off)
        struct.pack_into("<Q", b, le_filesize_off, cur_filesize + growth)
        sig_sz = new_sig_sz
    # Write the new SuperBlob region in place, zero-padded to sig_sz.
    new_region = bytearray(sig_sz)
    struct.pack_into(">III", new_region, 0, 0xfade0cc0, new_sb_length, len(entries))
    for i, (typ, rel) in enumerate(new_entries):
        struct.pack_into(">II", new_region, 12 + i * 8, typ, rel)
    new_region[12 + entries_sz : 12 + entries_sz + len(new_blobs)] = new_blobs
    b[sig_off : sig_off + sig_sz] = new_region
    open(path, "wb").write(b)
    PY

    # xnu refuses to load fat64 main executables (`fat64 main exec is
    # disallowed`), so the fat64 fixture is a dylib. Same rewrite
    # mechanism — the placeholder lands in the dylib's `__cstring`.
    cat > libgreet.c <<EOF
    #include <stdio.h>
    __attribute__((visibility("default"))) void greet(void) { puts(DOC_PATH); }
    EOF
    for arch in arm64 x86_64; do
      /usr/bin/cc -O0 -arch "$arch" -dynamiclib -Wl,-adhoc_codesign \
        -DDOC_PATH="\"$docPlaceholder\"" \
        -o "libgreet-$arch.dylib" libgreet.c
    done
    /usr/bin/lipo -create -fat64 libgreet-arm64.dylib libgreet-x86_64.dylib \
      -output libgreet-fat64.dylib

    mkdir -p "$out/bin" "$out/lib" "$out/share/fixtures" "$doc/share/doc"
    cp hello "$out/bin/hello"
    cp hello-self "$out/bin/hello-self"
    cp hello-codesigned "$out/bin/hello-codesigned"
    cp hello-fat32-1arch "$out/bin/hello-fat32-1arch"
    cp hello-fat32-multi "$out/bin/hello-fat32-multi"
    cp hello-cms "$out/bin/hello-cms"
    cp libgreet-fat64.dylib "$out/lib/libgreet-fat64.dylib"
    ln -s hello "$out/bin/hello-symlink"
    echo "hello docs" > "$doc/share/doc/hello"

    # 32 bytes clears the `sz < sizeof(mach_header) = 28` gate so the
    # helper reaches the `nfat` bound check instead of bailing early.
    { printf '\xca\xfe\xba\xbe\x00\x00\x00\x00'; head -c 24 /dev/zero; } \
      > "$out/share/fixtures/nfat-zero"
    # Java major version 65 (Java 21) reads as nfat=65 when the file
    # is misparsed as fat32.
    { printf '\xca\xfe\xba\xbe\x00\x00\x00\x41'; head -c 24 /dev/zero; } \
      > "$out/share/fixtures/java-class-shaped"
  '';
}
