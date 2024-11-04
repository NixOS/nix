# Options

The following options are allowed for all `nix-store` operations, but may not always have an effect.

- <span id="opt-add-root">[`--add-root`](#opt-add-root)</span> *path*

  Causes the result of a realisation (`--realise` and
  `--force-realise`) to be registered as a root of the garbage
  collector. *path* will be created as a symlink to the resulting
  store path. In addition, a uniquely named symlink to *path* will
  be created in `/nix/var/nix/gcroots/auto/`. For instance,

  ```console
  $ nix-store --add-root /home/eelco/bla/result --realise ...

  $ ls -l /nix/var/nix/gcroots/auto
  lrwxrwxrwx    1 ... 2005-03-13 21:10 dn54lcypm8f8... -> /home/eelco/bla/result

  $ ls -l /home/eelco/bla/result
  lrwxrwxrwx    1 ... 2005-03-13 21:10 /home/eelco/bla/result -> /nix/store/1r11343n6qd4...-f-spot-0.0.10
  ```

  Thus, when `/home/eelco/bla/result` is removed, the GC root in the
  `auto` directory becomes a dangling symlink and will be ignored by
  the collector.

  > **Warning**
  >
  > Note that it is not possible to move or rename GC roots, since
  > the symlink in the `auto` directory will still point to the old
  > location.

  If there are multiple results, then multiple symlinks will be
  created by sequentially numbering symlinks beyond the first one
  (e.g., `foo`, `foo-2`, `foo-3`, and so on).

