# Options

The following options are allowed for all `nix-env` operations, but may not always have an effect.

  - `--file` / `-f` *path*\
    Specifies the Nix expression (designated below as the *active Nix
    expression*) used by the `--install`, `--upgrade`, and `--query
    --available` operations to obtain derivations. The default is
    `~/.nix-defexpr`.

    If the argument starts with `http://` or `https://`, it is
    interpreted as the URL of a tarball that will be downloaded and
    unpacked to a temporary location. The tarball must include a single
    top-level directory containing at least a file named `default.nix`.

  - `--profile` / `-p` *path*\
    Specifies the profile to be used by those operations that operate on a profile, called the *active profile*.
    This will *not* change the `~/.nix-profile` symlink.

    > **Example**
    >
    > ```console
    > $ nix-env --profile /nix/var/nix/profiles/other-profile --install --attr nixpkgs.subversion
    > ```

    Be careful about storing a profile in another location than the [profiles directory](@docroot@/command-ref/files/profiles.md), since otherwise it might not be
    used as a root of the [garbage collector](@docroot@/command-ref/nix-store/gc).

  - `--dry-run`\
    For the `--install`, `--upgrade`, `--uninstall`,
    `--switch-generation`, `--delete-generations` and `--rollback`
    operations, this flag will cause `nix-env` to print what *would* be
    done if this flag had not been specified, without actually doing it.

    `--dry-run` also prints out which paths will be
    [substituted](@docroot@/glossary.md) (i.e., downloaded) and which paths
    will be built from source (because no substitute is available).

  - `--system-filter` *system*\
    By default, operations such as `--query
                    --available` show derivations matching any platform. This option
    allows you to use derivations for the specified platform *system*.
