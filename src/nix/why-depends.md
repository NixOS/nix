R""(

# Examples

* Show one path through the dependency graph leading from Hello to
  Glibc:

  ```console
  # nix why-depends nixpkgs#hello nixpkgs#glibc
  /nix/store/v5sv61sszx301i0x6xysaqzla09nksnd-hello-2.10
  └───bin/hello: …...................../nix/store/9l06v7fc38c1x3r2iydl15ksgz0ysb82-glibc-2.32/lib/ld-linux-x86-64.…
      → /nix/store/9l06v7fc38c1x3r2iydl15ksgz0ysb82-glibc-2.32
  ```

* Show all files and paths in the dependency graph leading from
  Thunderbird to libX11:

  ```console
  # nix why-depends --all nixpkgs#thunderbird nixpkgs#xorg.libX11
  /nix/store/qfc8729nzpdln1h0hvi1ziclsl3m84sr-thunderbird-78.5.1
  ├───lib/thunderbird/libxul.so: …6wrw-libxcb-1.14/lib:/nix/store/adzfjjh8w25vdr0xdx9x16ah4f5rqrw5-libX11-1.7.0/lib:/nix/store/ssf…
  │   → /nix/store/adzfjjh8w25vdr0xdx9x16ah4f5rqrw5-libX11-1.7.0
  ├───lib/thunderbird/libxul.so: …pxyc-libXt-1.2.0/lib:/nix/store/1qj29ipxl2fyi2b13l39hdircq17gnk0-libXdamage-1.1.5/lib:/nix/store…
  │   → /nix/store/1qj29ipxl2fyi2b13l39hdircq17gnk0-libXdamage-1.1.5
  │   ├───lib/libXdamage.so.1.1.0: …-libXfixes-5.0.3/lib:/nix/store/adzfjjh8w25vdr0xdx9x16ah4f5rqrw5-libX11-1.7.0/lib:/nix/store/9l0…
  │   │   → /nix/store/adzfjjh8w25vdr0xdx9x16ah4f5rqrw5-libX11-1.7.0
  …
  ```

* Show why Glibc depends on itself:

  ```console
  # nix why-depends nixpkgs#glibc nixpkgs#glibc
  /nix/store/9df65igwjmf2wbw0gbrrgair6piqjgmi-glibc-2.31
  └───lib/ld-2.31.so: …che       Do not use /nix/store/9df65igwjmf2wbw0gbrrgair6piqjgmi-glibc-2.31/etc/ld.so.cache.  --…
      → /nix/store/9df65igwjmf2wbw0gbrrgair6piqjgmi-glibc-2.31
  ```

* Show why Geeqie has a build-time dependency on `systemd`:

  ```console
  # nix why-depends --derivation nixpkgs#geeqie nixpkgs#systemd
  /nix/store/drrpq2fqlrbj98bmazrnww7hm1in3wgj-geeqie-1.4.drv
  └───/: …atch.drv",["out"]),("/nix/store/qzh8dyq3lfbk3i1acbp7x9wh3il2imiv-gtk+3-3.24.21.drv",["dev"]),("/…
      → /nix/store/qzh8dyq3lfbk3i1acbp7x9wh3il2imiv-gtk+3-3.24.21.drv
      └───/: …16.0.drv",["dev"]),("/nix/store/8kp79fyslf3z4m3dpvlh6w46iaadz5c2-cups-2.3.3.drv",["dev"]),("/nix…
          → /nix/store/8kp79fyslf3z4m3dpvlh6w46iaadz5c2-cups-2.3.3.drv
          └───/: ….3.1.drv",["out"]),("/nix/store/yd3ihapyi5wbz1kjacq9dbkaq5v5hqjg-systemd-246.4.drv",["dev"]),("/…
              → /nix/store/yd3ihapyi5wbz1kjacq9dbkaq5v5hqjg-systemd-246.4.drv
  ```

# Description

Nix automatically determines potential runtime dependencies between
store paths by scanning for the *hash parts* of store paths. For
instance, if there exists a store path
`/nix/store/9df65igwjmf2wbw0gbrrgair6piqjgmi-glibc-2.31`, and a file
inside another store path contains the string `9df65igw…`, then the
latter store path *refers* to the former, and thus might need it at
runtime. Nix always maintains the existence of the transitive closure
of a store path under the references relationship; it is therefore not
possible to install a store path without having all of its references
present.

Sometimes Nix packages end up with unexpected runtime dependencies;
for instance, a reference to a compiler might accidentally end up in a
binary, causing the former to be in the latter's closure. This kind of
*closure size bloat* is undesirable.

`nix why-depends` allows you to diagnose the cause of such issues. It
shows why the store path *package* depends on the store path
*dependency*, by showing a shortest sequence in the references graph
from the former to the latter. Also, for each node along this path, it
shows a file fragment containing a reference to the next store path in
the sequence.

To show why derivation *package* has a build-time rather than runtime
dependency on derivation *dependency*, use `--derivation`.

)""
