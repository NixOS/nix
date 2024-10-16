# Release 2.1 (2018-09-02)

This is primarily a bug fix release. It also reduces memory consumption
in certain situations. In addition, it has the following new features:

  - The Nix installer will no longer default to the Multi-User
    installation for macOS. You can still instruct the installer to
    run in multi-user mode.

  - The Nix installer now supports performing a Multi-User
    installation for Linux computers which are running systemd. You
    can select a Multi-User installation by passing the `--daemon`
    flag to the installer: `sh <(curl -L https://nixos.org/nix/install)
    --daemon`.

    The multi-user installer cannot handle systems with SELinux. If
    your system has SELinux enabled, you can force the installer to
    run in single-user mode.

  - New builtin functions: `builtins.bitAnd`, `builtins.bitOr`,
    `builtins.bitXor`, `builtins.fromTOML`, `builtins.concatMap`,
    `builtins.mapAttrs`.

  - The S3 binary cache store now supports uploading NARs larger than 5
    GiB.

  - The S3 binary cache store now supports uploading to S3-compatible
    services with the `endpoint` option.

  - The flag `--fallback` is no longer required to recover from
    disappeared NARs in binary caches.

  - `nix-daemon` now respects `--store`.

  - `nix run` now respects `nix-support/propagated-user-env-packages`.

This release has contributions from Adrien Devresse, Aleksandr Pashkov,
Alexandre Esteves, Amine Chikhaoui, Andrew Dunham, Asad Saeeduddin,
aszlig, Ben Challenor, Ben Gamari, Benjamin Hipple, Bogdan Seniuc, Corey
O'Connor, Daiderd Jordan, Daniel Peebles, Daniel Poelzleithner, Danylo
Hlynskyi, Dmitry Kalinkin, Domen Kožar, Doug Beardsley, Eelco Dolstra,
Erik Arvstedt, Félix Baylac-Jacqué, Gleb Peregud, Graham Christensen,
Guillaume Maudoux, Ivan Kozik, John Arnold, Justin Humm, Linus
Heckemann, Lorenzo Manacorda, Matthew Justin Bauer, Matthew O'Gorman,
Maximilian Bosch, Michael Bishop, Michael Fiano, Michael Mercier,
Michael Raskin, Michael Weiss, Nicolas Dudebout, Peter Simons, Ryan
Trinkle, Samuel Dionne-Riel, Sean Seefried, Shea Levy, Symphorien Gibol,
Tim Engler, Tim Sears, Tuomas Tynkkynen, volth, Will Dietz, Yorick van
Pelt and zimbatm.
