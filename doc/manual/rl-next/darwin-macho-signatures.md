---
synopsis: "Store path hash rewrites that would break a Mach-O code signature are now refused"
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

The new `macho-signature-rewrite-check` setting (`refuse` by default) makes
the build fail instead, with an error naming the affected files and the
already-present store paths whose deletion allows a clean rebuild. This also
replaces the spurious "may not be deterministic" failure previously reported
for signed binaries under `--check`, and makes content-addressed cold builds
of self-referential signed binaries fail loudly rather than register silently
broken outputs. `warn` and `ignore` restore the previous behaviour with and
without a diagnostic.

Detection is content-based, so cross-builds of macOS binaries on other
platforms are covered too.
