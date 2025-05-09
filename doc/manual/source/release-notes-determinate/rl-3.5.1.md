# Release 3.5.1 (2025-05-09)

* Based on [upstream Nix 2.28.3](../release-notes/rl-2.28.md).

## What's Changed

Most notably, Lazy Trees has merged in to Determinate Nix and is in Feature Preview status, but remains disabled by default.
Lazy trees massively improves performance in virtually all scenarios because it enables Nix to avoid making unnecessary copies of files into the Nix store.
In testing, we saw iteration times on Nixpkgs **drop from over 12 seconds to 3.5 seconds**.

After upgrading to Determinate Nix 3.5.1 with `sudo determinate-nixd upgrade`, enable lazy trees by adding this to `/etc/nix/nix.custom.conf`:

```
lazy-trees = true
```

Please note that our full flake regression test suite passes with no changes with lazy trees, and please report compatibility issues.

Read [this GitHub comment](https://github.com/DeterminateSystems/nix-src/pull/27#pullrequestreview-2822153088) for further details and next steps.
We'll be publishing an update on the [Determinate Systems blog](https://determinate.systems/posts/) in the next few days with more information as well.

Relevant PRs:
* Lazy trees v2 by @edolstra in [DeterminateSystems/nix-src#27](https://github.com/DeterminateSystems/nix-src/pull/27)
* Improve lazy trees backward compatibility by @edolstra in [DeterminateSystems/nix-src#56](https://github.com/DeterminateSystems/nix-src/pull/56)


### Additional changes in this release:
* Bug fix: Flake input URLs are canonicalized before checking flake.lock file staleness, avoiding needlessly regenerating flake.lock files with `dir` in URL-style flakerefs by @edolstra in [DeterminateSystems/nix-src#57](https://github.com/DeterminateSystems/nix-src/pull/57)
* `nix upgrade-nix` is deprecated in favor of `determinate-nixd upgrade`, by @gustavderdrache in [DeterminateSystems/nix-src#55](https://github.com/DeterminateSystems/nix-src/pull/55)
* UX: Improved build failure and dependency failure error messages to include needed output paths by @edolstra in [DeterminateSystems/nix-src#58](https://github.com/DeterminateSystems/nix-src/pull/58).

Previously:

```
error: builder for '/nix/store/[...]-nested-failure-bottom.drv' failed with exit code 1
error: 1 dependencies of derivation '/nix/store/[...]-nested-failure-middle.drv' failed to build
error: 1 dependencies of derivation '/nix/store/[...]-nested-failure-top.drv' failed to build
```

Now:

```
error: Cannot build '/nix/store/w37gflm9wz9dcnsgy3sfrmnlvm8qigaj-nested-failure-bottom.drv'.
       Reason: builder failed with exit code 1.
       Output paths:
         /nix/store/yzybs8kp35dfipbzdlqcc6lxz62hax04-nested-failure-bottom
error: Cannot build '/nix/store/00gr5hlxfc03x2675w6nn3pwfrz2fr62-nested-failure-middle.drv'.
       Reason: 1 dependency failed.
       Output paths:
         /nix/store/h781j5h4bdchmb4c2lvy8qzh8733azhz-nested-failure-middle
error: Cannot build '/nix/store/8am0ng1gyx8sbzyr0yx6jd5ix3yy5szc-nested-failure-top.drv'.
       Reason: 1 dependency failed.
       Output paths:
         /nix/store/fh12637kgvp906s9yhi9w2dc7ghfwxs1-nested-failure-top
```

**Full Changelog**: [v3.4.2...v3.5.1](https://github.com/DeterminateSystems/nix-src/compare/v3.4.2...v3.5.1)
