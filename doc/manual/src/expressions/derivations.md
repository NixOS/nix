# Derivations

The most important built-in function is `derivation`, which is used to
describe a single derivation (a build action). It takes as input a set,
the attributes of which specify the inputs of the build.

  - There must be an attribute named `system` whose value must be a
    string specifying a Nix system type, such as `"i686-linux"` or
    `"x86_64-darwin"`. (To figure out your system type, run `nix -vv
    --version`.) The build can only be performed on a machine and
    operating system matching the system type. (Nix can automatically
    [forward builds for other
    platforms](../advanced-topics/distributed-builds.md) by forwarding
    them to other machines.)

  - There must be an attribute named `name` whose value must be a
    string. This is used as a symbolic name for the package by
    `nix-env`, and it is appended to the output paths of the derivation.

  - There must be an attribute named `builder` that identifies the
    program that is executed to perform the build. It can be either a
    derivation or a source (a local file reference, e.g.,
    `./builder.sh`).

  - Every attribute is passed as an environment variable to the builder.
    Attribute values are translated to environment variables as follows:
    
      - Strings and numbers are just passed verbatim.
    
      - A *path* (e.g., `../foo/sources.tar`) causes the referenced file
        to be copied to the store; its location in the store is put in
        the environment variable. The idea is that all sources should
        reside in the Nix store, since all inputs to a derivation should
        reside in the Nix store.
    
      - A *derivation* causes that derivation to be built prior to the
        present derivation; its default output path is put in the
        environment variable.
    
      - Lists of the previous types are also allowed. They are simply
        concatenated, separated by spaces.
    
      - `true` is passed as the string `1`, `false` and `null` are
        passed as an empty string.

  - The optional attribute `args` specifies command-line arguments to be
    passed to the builder. It should be a list.

  - The optional attribute `outputs` specifies a list of symbolic
    outputs of the derivation. By default, a derivation produces a
    single output path, denoted as `out`. However, derivations can
    produce multiple output paths. This is useful because it allows
    outputs to be downloaded or garbage-collected separately. For
    instance, imagine a library package that provides a dynamic library,
    header files, and documentation. A program that links against the
    library doesn’t need the header files and documentation at runtime,
    and it doesn’t need the documentation at build time. Thus, the
    library package could specify:
    
    ```nix
    outputs = [ "lib" "headers" "doc" ];
    ```
    
    This will cause Nix to pass environment variables `lib`, `headers`
    and `doc` to the builder containing the intended store paths of each
    output. The builder would typically do something like
    
    ```bash
    ./configure \
      --libdir=$lib/lib \
      --includedir=$headers/include \
      --docdir=$doc/share/doc
    ```
    
    for an Autoconf-style package. You can refer to each output of a
    derivation by selecting it as an attribute, e.g.
    
    ```nix
    buildInputs = [ pkg.lib pkg.headers ];
    ```
    
    The first element of `outputs` determines the *default output*.
    Thus, you could also write
    
    ```nix
    buildInputs = [ pkg pkg.headers ];
    ```
    
    since `pkg` is equivalent to `pkg.lib`.

The function `mkDerivation` in the Nixpkgs standard environment is a
wrapper around `derivation` that adds a default value for `system` and
always uses Bash as the builder, to which the supplied builder is passed
as a command-line argument. See the Nixpkgs manual for details.

The builder is executed as follows:

  - A temporary directory is created under the directory specified by
    `TMPDIR` (default `/tmp`) where the build will take place. The
    current directory is changed to this directory.

  - The environment is cleared and set to the derivation attributes, as
    specified above.

  - In addition, the following variables are set:
    
      - `NIX_BUILD_TOP` contains the path of the temporary directory for
        this build.
    
      - Also, `TMPDIR`, `TEMPDIR`, `TMP`, `TEMP` are set to point to the
        temporary directory. This is to prevent the builder from
        accidentally writing temporary files anywhere else. Doing so
        might cause interference by other processes.
    
      - `PATH` is set to `/path-not-set` to prevent shells from
        initialising it to their built-in default value.
    
      - `HOME` is set to `/homeless-shelter` to prevent programs from
        using `/etc/passwd` or the like to find the user's home
        directory, which could cause impurity. Usually, when `HOME` is
        set, it is used as the location of the home directory, even if
        it points to a non-existent path.
    
      - `NIX_STORE` is set to the path of the top-level Nix store
        directory (typically, `/nix/store`).
    
      - For each output declared in `outputs`, the corresponding
        environment variable is set to point to the intended path in the
        Nix store for that output. Each output path is a concatenation
        of the cryptographic hash of all build inputs, the `name`
        attribute and the output name. (The output name is omitted if
        it’s `out`.)

  - If an output path already exists, it is removed. Also, locks are
    acquired to prevent multiple Nix instances from performing the same
    build at the same time.

  - A log of the combined standard output and error is written to
    `/nix/var/log/nix`.

  - The builder is executed with the arguments specified by the
    attribute `args`. If it exits with exit code 0, it is considered to
    have succeeded.

  - The temporary directory is removed (unless the `-K` option was
    specified).

  - If the build was successful, Nix scans each output path for
    references to input paths by looking for the hash parts of the input
    paths. Since these are potential runtime dependencies, Nix registers
    them as dependencies of the output paths.

  - After the build, Nix sets the last-modified timestamp on all files
    in the build result to 1 (00:00:01 1/1/1970 UTC), sets the group to
    the default group, and sets the mode of the file to 0444 or 0555
    (i.e., read-only, with execute permission enabled if the file was
    originally executable). Note that possible `setuid` and `setgid`
    bits are cleared. Setuid and setgid programs are not currently
    supported by Nix. This is because the Nix archives used in
    deployment have no concept of ownership information, and because it
    makes the build result dependent on the user performing the build.
