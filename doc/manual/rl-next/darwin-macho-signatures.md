---
synopsis: "macOS code-signature validity is now checked when store paths are registered"
issues: [6065]
prs: [15638]
---

On macOS, rewriting store path hashes inside a signed Mach-O binary — which
Nix does when registering an output that references a path already present in
the store, and for self-references in content-addressed builds — invalidates
the signature's page hashes. The kernel then kills the binary the first time
it is run. Previously this corruption was silent; the build "succeeded" and
the registered binary was broken (see the [`fish`
issue](https://github.com/NixOS/nixpkgs/issues/507531)).

Nix now treats a valid Mach-O signature as a property to preserve when bytes
enter the store, controlled by three settings:

- `macho-signature-rewrite-check` (`refuse` by default) checks build outputs
  before the rewrite. Under `refuse`, the build fails with an error naming
  the affected files; the new `macho-signature-repair-hook` (an internal tool by
  default, run with the privileges of a build user) then deterministically
  repairs the stale page hashes in place, touching no other byte, and the
  output is registered only once the repaired signatures verify — so most
  builds succeed transparently. `warn` and `ignore` are also available.

- `macho-signature-verify` (`ignore` by default) checks paths obtained from
  a substituter — where binaries broken elsewhere actually reach users — and
  can `warn`, `refuse`, or `repair` them. A signature that cannot be
  verified (a file too large to parse, an unsupported hash type) is treated
  as invalid rather than waved through.

- `nix store fixup-macho` repairs broken signatures in paths already in the
  store.

Detection is content-based, so cross-builds of macOS binaries on other
platforms are covered too. Files signed with a certificate (Developer ID)
and self-referential content-addressed outputs cannot be repaired and are
reported rather than silently altered.
