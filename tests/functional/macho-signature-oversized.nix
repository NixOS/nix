with import ./config.nix;

# A single-output derivation containing a file that carries Mach-O
# magic but exceeds the parser's file-size limit (512 MiB). Such a
# file is reported `Unchecked` by the daemon-side scan and skipped by
# the check child — it can never be verified, only refused or waved
# through with a warning.

mkDerivation {
  name = "macho-signature-oversized";
  buildCommand = ''
    mkdir -p "$out"
    # 512 MiB + 1 byte, starting with MH_MAGIC_64. Sparse where the
    # filesystem allows; NAR serialisation stores the zeros anyway.
    printf '\xcf\xfa\xed\xfe' > "$out/big"
    dd if=/dev/zero of="$out/big" bs=1 count=1 seek=536870912 conv=notrunc 2>/dev/null
  '';
}
