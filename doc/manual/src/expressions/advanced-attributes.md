# Advanced Attributes

Derivations can declare some infrequently used optional attributes.

  - `allowedReferences`\
    The optional attribute `allowedReferences` specifies a list of legal
    references (dependencies) of the output of the builder. For example,

    ```nix
    allowedReferences = [];
    ```

    enforces that the output of a derivation cannot have any runtime
    dependencies on its inputs. To allow an output to have a runtime
    dependency on itself, use `"out"` as a list item. This is used in
    NixOS to check that generated files such as initial ramdisks for
    booting Linux don’t have accidental dependencies on other paths in
    the Nix store.

  - `allowedRequisites`\
    This attribute is similar to `allowedReferences`, but it specifies
    the legal requisites of the whole closure, so all the dependencies
    recursively. For example,

    ```nix
    allowedRequisites = [ foobar ];
    ```

    enforces that the output of a derivation cannot have any other
    runtime dependency than `foobar`, and in addition it enforces that
    `foobar` itself doesn't introduce any other dependency itself.

  - `disallowedReferences`\
    The optional attribute `disallowedReferences` specifies a list of
    illegal references (dependencies) of the output of the builder. For
    example,

    ```nix
    disallowedReferences = [ foo ];
    ```

    enforces that the output of a derivation cannot have a direct
    runtime dependencies on the derivation `foo`.

  - `disallowedRequisites`\
    This attribute is similar to `disallowedReferences`, but it
    specifies illegal requisites for the whole closure, so all the
    dependencies recursively. For example,

    ```nix
    disallowedRequisites = [ foobar ];
    ```

    enforces that the output of a derivation cannot have any runtime
    dependency on `foobar` or any other derivation depending recursively
    on `foobar`.

  - `exportReferencesGraph`\
    This attribute allows builders access to the references graph of
    their inputs. The attribute is a list of inputs in the Nix store
    whose references graph the builder needs to know. The value of
    this attribute should be a list of pairs `[ name1 path1 name2
    path2 ...  ]`. The references graph of each *pathN* will be stored
    in a text file *nameN* in the temporary build directory. The text
    files have the format used by `nix-store --register-validity`
    (with the deriver fields left empty). For example, when the
    following derivation is built:

    ```nix
    derivation {
      ...
      exportReferencesGraph = [ "libfoo-graph" libfoo ];
    };
    ```

    the references graph of `libfoo` is placed in the file
    `libfoo-graph` in the temporary build directory.

    `exportReferencesGraph` is useful for builders that want to do
    something with the closure of a store path. Examples include the
    builders in NixOS that generate the initial ramdisk for booting
    Linux (a `cpio` archive containing the closure of the boot script)
    and the ISO-9660 image for the installation CD (which is populated
    with a Nix store containing the closure of a bootable NixOS
    configuration).

  - `impureEnvVars`\
    This attribute allows you to specify a list of environment variables
    that should be passed from the environment of the calling user to
    the builder. Usually, the environment is cleared completely when the
    builder is executed, but with this attribute you can allow specific
    environment variables to be passed unmodified. For example,
    `fetchurl` in Nixpkgs has the line

    ```nix
    impureEnvVars = [ "http_proxy" "https_proxy" ... ];
    ```

    to make it use the proxy server configuration specified by the user
    in the environment variables `http_proxy` and friends.

    This attribute is only allowed in *fixed-output derivations* (see
    below), where impurities such as these are okay since (the hash
    of) the output is known in advance. It is ignored for all other
    derivations.

    > **Warning**
    >
    > `impureEnvVars` implementation takes environment variables from
    > the current builder process. When a daemon is building its
    > environmental variables are used. Without the daemon, the
    > environmental variables come from the environment of the
    > `nix-build`.

  - `outputHash`; `outputHashAlgo`; `outputHashMode`\
    These attributes declare that the derivation is a so-called
    *fixed-output derivation*, which means that a cryptographic hash of
    the output is already known in advance. When the build of a
    fixed-output derivation finishes, Nix computes the cryptographic
    hash of the output and compares it to the hash declared with these
    attributes. If there is a mismatch, the build fails.

    The rationale for fixed-output derivations is derivations such as
    those produced by the `fetchurl` function. This function downloads a
    file from a given URL. To ensure that the downloaded file has not
    been modified, the caller must also specify a cryptographic hash of
    the file. For example,

    ```nix
    fetchurl {
      url = "http://ftp.gnu.org/pub/gnu/hello/hello-2.1.1.tar.gz";
      sha256 = "1md7jsfd8pa45z73bz1kszpp01yw6x5ljkjk2hx7wl800any6465";
    }
    ```

    It sometimes happens that the URL of the file changes, e.g., because
    servers are reorganised or no longer available. We then must update
    the call to `fetchurl`, e.g.,

    ```nix
    fetchurl {
      url = "ftp://ftp.nluug.nl/pub/gnu/hello/hello-2.1.1.tar.gz";
      sha256 = "1md7jsfd8pa45z73bz1kszpp01yw6x5ljkjk2hx7wl800any6465";
    }
    ```

    If a `fetchurl` derivation was treated like a normal derivation, the
    output paths of the derivation and *all derivations depending on it*
    would change. For instance, if we were to change the URL of the
    Glibc source distribution in Nixpkgs (a package on which almost all
    other packages depend) massive rebuilds would be needed. This is
    unfortunate for a change which we know cannot have a real effect as
    it propagates upwards through the dependency graph.

    For fixed-output derivations, on the other hand, the name of the
    output path only depends on the `outputHash*` and `name` attributes,
    while all other attributes are ignored for the purpose of computing
    the output path. (The `name` attribute is included because it is
    part of the path.)

    As an example, here is the (simplified) Nix expression for
    `fetchurl`:

    ```nix
    { stdenv, curl }: # The curl program is used for downloading.

    { url, sha256 }:

    stdenv.mkDerivation {
      name = baseNameOf (toString url);
      builder = ./builder.sh;
      buildInputs = [ curl ];

      # This is a fixed-output derivation; the output must be a regular
      # file with SHA256 hash sha256.
      outputHashMode = "flat";
      outputHashAlgo = "sha256";
      outputHash = sha256;

      inherit url;
    }
    ```

    The `outputHashAlgo` attribute specifies the hash algorithm used to
    compute the hash. It can currently be `"sha1"`, `"sha256"` or
    `"sha512"`.

    The `outputHashMode` attribute determines how the hash is computed.
    It must be one of the following two values:

      - `"flat"`\
        The output must be a non-executable regular file. If it isn’t,
        the build fails. The hash is simply computed over the contents
        of that file (so it’s equal to what Unix commands like
        `sha256sum` or `sha1sum` produce).

        This is the default.

      - `"recursive"`\
        The hash is computed over the NAR archive dump of the output
        (i.e., the result of [`nix-store
        --dump`](../command-ref/nix-store.md#operation---dump)). In
        this case, the output can be anything, including a directory
        tree.

    The `outputHash` attribute, finally, must be a string containing
    the hash in either hexadecimal or base-32 notation. (See the
    [`nix-hash` command](../command-ref/nix-hash.md) for information
    about converting to and from base-32 notation.)
    
  - `__contentAddressed`
    If this **experimental** attribute is set to true, then the derivation
    outputs will be stored in a content-addressed location rather than the
    traditional input-addressed one.
    This only has an effect if the `ca-derivation` experimental feature is enabled.
    
    Setting this attribute also requires setting `outputHashMode` and `outputHashAlgo` like for *fixed-output derivations* (see above).

  - `passAsFile`\
    A list of names of attributes that should be passed via files rather
    than environment variables. For example, if you have

    ```nix
    passAsFile = ["big"];
    big = "a very long string";
    ```

    then when the builder runs, the environment variable `bigPath`
    will contain the absolute path to a temporary file containing `a
    very long string`. That is, for any attribute *x* listed in
    `passAsFile`, Nix will pass an environment variable `xPath`
    holding the path of the file containing the value of attribute
    *x*. This is useful when you need to pass large strings to a
    builder, since most operating systems impose a limit on the size
    of the environment (typically, a few hundred kilobyte).

  - `preferLocalBuild`\
    If this attribute is set to `true` and [distributed building is
    enabled](../advanced-topics/distributed-builds.md), then, if
    possible, the derivation will be built locally instead of forwarded
    to a remote machine. This is appropriate for trivial builders
    where the cost of doing a download or remote build would exceed
    the cost of building locally.

  - `allowSubstitutes`\
    If this attribute is set to `false`, then Nix will always build this
    derivation; it will not try to substitute its outputs. This is
    useful for very trivial derivations (such as `writeText` in Nixpkgs)
    that are cheaper to build than to substitute from a binary cache.

    > **Note**
    >
    > You need to have a builder configured which satisfies the
    > derivation’s `system` attribute, since the derivation cannot be
    > substituted. Thus it is usually a good idea to align `system` with
    > `builtins.currentSystem` when setting `allowSubstitutes` to
    > `false`. For most trivial derivations this should be the case.
