# Release X.Y (202?-??-??)

* Various nix commands can now read expressions from stdin with `--file -`.

* `nix store make-content-addressable` has been renamed to `nix store
  make-content-addressed`.

* New experimental builtin function `builtins.fetchClosure` that
  copies a closure from a binary cache at evaluation time and rewrites
  it to content-addressed form (if it isn't already). Like
  `builtins.storePath`, this allows importing pre-built store paths;
  the difference is that it doesn't require the user to configure
  binary caches and trusted public keys.

  This function is only available if you enable the experimental
  feature `fetch-closure`.
