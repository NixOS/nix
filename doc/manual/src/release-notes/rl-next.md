# Release X.Y (202?-??-??)

* The `repeat` and `enforce-determinism` options have been removed
  since they had been broken under many circumstances for a long time.

* Allow explicitly selecting outputs in a store derivation installable, just like we can do with other sorts of installables.
  For example,
  ```shell-session
  $ nix-build /nix/store/gzaflydcr6sb3567hap9q6srzx8ggdgg-glibc-2.33-78.drv^dev`
  ```
  now works just as
  ```shell-session
  $ nix-build glibc^dev`
  ```
  does already.
