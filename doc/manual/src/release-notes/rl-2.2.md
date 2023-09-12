# Release 2.2 (2019-01-11)

This is primarily a bug fix release. It also has the following changes:

  - In derivations that use structured attributes (i.e. that specify set
    the `__structuredAttrs` attribute to `true` to cause all attributes
    to be passed to the builder in JSON format), you can now specify
    closure checks per output, e.g.:
    
        outputChecks."out" = {
          # The closure of 'out' must not be larger than 256 MiB.
          maxClosureSize = 256 * 1024 * 1024;
        
          # It must not refer to C compiler or to the 'dev' output.
          disallowedRequisites = [ stdenv.cc "dev" ];
        };
        
        outputChecks."dev" = {
          # The 'dev' output must not be larger than 128 KiB.
          maxSize = 128 * 1024;
        };

  - The derivation attribute `requiredSystemFeatures` is now enforced
    for local builds, and not just to route builds to remote builders.
    The supported features of a machine can be specified through the
    configuration setting `system-features`.
    
    By default, `system-features` includes `kvm` if `/dev/kvm` exists.
    For compatibility, it also includes the pseudo-features
    `nixos-test`, `benchmark` and `big-parallel` which are used by
    Nixpkgs to route builds to particular Hydra build machines.

  - Sandbox builds are now enabled by default on Linux.

  - The new command `nix doctor` shows potential issues with your Nix
    installation.

  - The `fetchGit` builtin function now uses a caching scheme that puts
    different remote repositories in distinct local repositories, rather
    than a single shared repository. This may require more disk space
    but is faster.

  - The `dirOf` builtin function now works on relative paths.

  - Nix now supports [SRI hashes](https://www.w3.org/TR/SRI/), allowing
    the hash algorithm and hash to be specified in a single string. For
    example, you can write:
    
        import <nix/fetchurl.nix> {
          url = https://nixos.org/releases/nix/nix-2.1.3/nix-2.1.3.tar.xz;
          hash = "sha256-XSLa0FjVyADWWhFfkZ2iKTjFDda6mMXjoYMXLRSYQKQ=";
        };
    
    instead of
    
        import <nix/fetchurl.nix> {
          url = https://nixos.org/releases/nix/nix-2.1.3/nix-2.1.3.tar.xz;
          sha256 = "5d22dad058d5c800d65a115f919da22938c50dd6ba98c5e3a183172d149840a4";
        };
    
    In fixed-output derivations, the `outputHashAlgo` attribute is no
    longer mandatory if `outputHash` specifies the hash.
    
    `nix hash-file` and `nix
                    hash-path` now print hashes in SRI format by default. They also use
    SHA-256 by default instead of SHA-512 because that's what we use
    most of the time in Nixpkgs.

  - Integers are now 64 bits on all platforms.

  - The evaluator now prints profiling statistics (enabled via the
    `NIX_SHOW_STATS` and `NIX_COUNT_CALLS` environment variables) in
    JSON format.

  - The option `--xml` in `nix-store
                    --query` has been removed. Instead, there now is an option
    `--graphml` to output the dependency graph in GraphML format.

  - All `nix-*` commands are now symlinks to `nix`. This saves a bit of
    disk space.

  - `nix repl` now uses `libeditline` or `libreadline`.
