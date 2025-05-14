# Lockable HTTP Tarball Protocol

Tarball flakes can be served as regular tarballs via HTTP or the file
system (for `file://` URLs). Unless the server implements the Lockable
HTTP Tarball protocol, it is the responsibility of the user to make sure that
the URL always produces the same tarball contents.

An HTTP server can return an "immutable" HTTP URL appropriate for lock
files. This allows users to specify a tarball flake input in
`flake.nix` that requests the latest version of a flake
(e.g. `https://example.org/hello/latest.tar.gz`), while `flake.lock`
will record a URL whose contents will not change
(e.g. `https://example.org/hello/<revision>.tar.gz`). To do so, the
server must return an [HTTP `Link` header](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Link) with the `rel` attribute set to
`immutable`, as follows:

```
Link: <flakeref>; rel="immutable"
```

(Note the required `<` and `>` characters around *flakeref*.)

*flakeref* must be a tarball flakeref. It can contain the tarball flake attributes
`narHash`, `rev`, `revCount` and `lastModified`. If `narHash` is included, its
value must be the [NAR hash][Nix Archive] of the unpacked tarball (as computed via
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

## Gitea and Forgejo support

This protocol is supported by Gitea since v1.22.1 and by Forgejo since v7.0.4/v8.0.0 and can be used with the following flake URL schema:

```
https://<domain name>/<owner>/<repo>/archive/<reference or revision>.tar.gz
```

> **Example**
>
>
> ```nix
> # flake.nix
> {
>    inputs = {
>      foo.url = "https://gitea.example.org/some-person/some-flake/archive/main.tar.gz";
>      bar.url = "https://gitea.example.org/some-other-person/other-flake/archive/442793d9ec0584f6a6e82fa253850c8085bb150a.tar.gz";
>      qux = {
>        url = "https://forgejo.example.org/another-person/some-non-flake-repo/archive/development.tar.gz";
>        flake = false;
>      };
>    };
>    outputs = { foo, bar, qux }: { /* ... */ };
> }
```

[Nix Archive]: @docroot@/store/file-system-object/content-address.md#serial-nix-archive
