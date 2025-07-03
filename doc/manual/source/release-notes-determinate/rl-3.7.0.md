# Release 3.7.0 (2025-07-03)

- Based on [upstream Nix 2.29.1](../release-notes/rl-2.29.md).

## What's Changed

### Prefetch flake inputs in parallel

By @edolstra in [DeterminateSystems/nix-src#127](https://github.com/DeterminateSystems/nix-src/pull/127)

This release brings the command `nix flake prefetch-inputs`.

Flake inputs are typically fetched "just in time."
That means Nix fetches a flake input when the evaluator needs it, and not before.
When the evaluator needs an input, evaluation is paused until the source is available.

This causes a significant slow-down on projects with lots of flake inputs.

The new command `nix flake prefetch-inputs` fetches all flake inputs in parallel.
We expect running this new command before building will dramatically improve evaluation performance for most projects, especially in CI.
Note that projects which with many unused flake inputs may not benefit from this change, since the new command fetches every input whether they're used or not.

### Deep flake input overrides now work as expected

By @edolstra in [DeterminateSystems/nix-src#108](https://github.com/DeterminateSystems/nix-src/pull/108)

An override like:

```
inputs.foo.inputs.bar.inputs.nixpkgs.follows = "nixpkgs";
```

implicitly set `inputs.foo.inputs.bar` to `flake:bar`, which led to an unexpected error like:

```
error: cannot find flake 'flake:bar' in the flake registries
```

We now no longer create a parent override (like for `foo.bar` in the example above) if it doesn't set an explicit ref or follows attribute.
We only recursively apply its child overrides.

### `nix store delete` now shows you why deletion was not possible

By @edolstra in [DeterminateSystems/nix-src#130](https://github.com/DeterminateSystems/nix-src/pull/130)

For example:

```
error: Cannot delete path '/nix/store/6fcrjgfjip2ww3sx51rrmmghfsf60jvi-patchelf-0.14.3'
  because it's referenced by the GC root '/home/eelco/Dev/nix-master/build/result'.

error: Cannot delete path '/nix/store/rn0qyn3kmky26xgpr2n10vr787g57lff-cowsay-3.8.4'
  because it's referenced by the GC root '/proc/3600568/environ'.

error: Cannot delete path '/nix/store/klyng5rpdkwi5kbxkncy4gjwb490dlhb-foo.drv'
  because it's in use by '{nix-process:3605324}'.
```

### Lazy-tree improvements

- Improved lazy-tree evaluation caching for flakes accessed with a `path` flakeref by @edolstra in [DeterminateSystems/nix-src#131](https://github.com/DeterminateSystems/nix-src/pull/131)

**Full Changelog**: [v3.6.8...v3.7.0](https://github.com/DeterminateSystems/nix-src/compare/v3.6.8...v3.7.0)
