R""(

# Examples

* Check which store paths carry Mach-O files with invalid code
  signatures:

  ```console
  # nix store fixup-macho --dry-run --all
  would repair '/nix/store/hzi7bnyfj6ic73b3rnjjjhq9mgx1v9nh-fish-4.2.1'
  1 path(s) with invalid Mach-O signatures
  ```

* Repair one path:

  ```console
  # nix store fixup-macho /nix/store/hzi7bnyfj6ic73b3rnjjjhq9mgx1v9nh-fish-4.2.1
  repaired '/nix/store/hzi7bnyfj6ic73b3rnjjjhq9mgx1v9nh-fish-4.2.1'
  ```

# Description

This command finds Mach-O files whose code-signature page hashes do
not match their contents inside the given store paths, and repairs
them by recomputing the stale hashes in place. Such binaries are
killed by the macOS kernel when they are first executed; they can end
up in a store via substitution of an artifact that was already broken
where it was built (see the [`macho-signature-verify`](@docroot@/command-ref/conf-file.md#conf-macho-signature-verify)
setting for catching that at substitution time), or via builds
performed by older Nix versions.

The repair never modifies a path's files in place: with
`auto-optimise-store`, files may be hard-linked into other store
paths, and an in-place write would corrupt every path sharing the
inode. Instead the path's contents are copied, repaired, and swapped
in, and the path's NAR hash is updated in the Nix database. Since the
new hash differs from what any substituter advertised, the path's
signatures no longer apply and are dropped — the repaired path is
registered unsigned.

Content-addressed store paths are skipped: repairing one would change
its contents away from its own content address. Files signed with a
certificate (Developer ID) are also not repairable, since only the
original signing identity can produce a valid signature. A path is
skipped whole if any of its Mach-O files carries such a signature, so
an ad-hoc-signed file (or the ad-hoc-signed slice of a mixed
universal binary) sharing a path with a certificate-signed one is
left unrepaired — the same conservative choice the build-time and
substitution-time checks make.

> **Warning**
>
> As with `nix store repair`, there is a small window during which
> the old path is moved out of the way and replaced. If the command
> is interrupted in that window, the path may be left missing, or —
> if interrupted after the swap but before the database update — left
> with repaired contents whose recorded NAR hash no longer matches.
> Both states are detected by `nix store verify` and recovered with
> `nix store verify --repair`, which re-obtains the path from a
> substituter or by rebuilding rather than trusting the on-disk
> bytes.

)""
