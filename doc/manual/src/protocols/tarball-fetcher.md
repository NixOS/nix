# Serving Tarball Flakes

Tarball flakes are served as regular tarballs via HTTP or the file
system (for `file://` URLs).

An HTTP server can return an "immutable" flakeref appropriate for lock
files. This allows users to specify a tarball flake input in
`flake.nix` that requests the latest version of a flake
(e.g. `https://example.org/hello/latest.tar.gz`), while `flake.lock`
will record a URL whose contents will not change
(e.g. `https://example.org/hello/<revision>.tar.gz`). To do so, the
server must return a `Link` header with the `rel` attribute set to
`immutable`, as follows:

```
Link: <flakeref>; rel="immutable"
```

(Note the required `<` and `>` characters around *flakeref*.)

*flakeref* must be a tarball flakeref. It can contain flake attributes
such as `narHash`, `rev` and `revCount`. If `narHash` is included, its
value must be the NAR hash of the unpacked tarball (as computed via
`nix hash path`). Nix checks the contents of the returned tarball
against the `narHash` attribute. The `rev` and `revCount` attributes
are useful when the tarball flake is a mirror of a fetcher type that
has those attributes, such as Git or GitHub. They are not checked by
Nix.

```
Link: <https://example.org/hello/442793d9ec0584f6a6e82fa253850c8085bb150a.tar.gz
  ?rev=442793d9ec0584f6a6e82fa253850c8085bb150a
  &revCount=835
  &narHash=sha256-GUm8Uh/U74zFCwkvt9Mri4DSM%2BmHj3tYhXUkYpiv31M%3D>; rel="immutable"
```

(The linebreaks in this example are for clarity and must not be included in the actual response.)

For tarball flakes, the value of the `lastModified` flake attribute is
defined as the timestamp of the newest file inside the tarball.
