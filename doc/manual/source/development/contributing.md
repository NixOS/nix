# Contributing

## Add a release note

`doc/manual/rl-next` contains release notes entries for all unreleased changes.

User-visible changes should come with a release note.

### Add an entry

Here's what a complete entry looks like. The file name is not incorporated in the document.

```
---
synopsis: Basically a title
issues: 1234
prs: 1238
---

Here's one or more paragraphs that describe the change.

- It's markdown
- Add references to the manual using [links like this](@_at_docroot@/example.md)
```
<!-- for the raw markdown readers: that means using @docroot@ -->

Significant changes should add the following header, which moves them to the top.

```
significance: significant
```

<!-- Keep an eye on https://codeberg.org/fgaz/changelog-d/issues/1 -->
See also the [format documentation](https://github.com/haskell/cabal/blob/master/CONTRIBUTING.md#changelog).

### Build process

Releases have a precomputed `rl-MAJOR.MINOR.md`, and no `rl-next.md`.

## Branches

- [`master`](https://github.com/NixOS/nix/commits/master)

  The main development branch. All changes are approved and merged here.
  When developing a change, create a branch based on the latest `master`.

  Maintainers try to [keep it in a release-worthy state](#reverting).

- [`maintenance-*.*`](https://github.com/NixOS/nix/branches/all?query=maintenance)

  These branches are the subject of backports only, and are
  also [kept](#reverting) in a release-worthy state.

  See [`maintainers/backporting.md`](https://github.com/NixOS/nix/blob/master/maintainers/backporting.md)

- [`latest-release`](https://github.com/NixOS/nix/tree/latest-release)

  The latest patch release of the latest minor version.

  See [`maintainers/release-process.md`](https://github.com/NixOS/nix/blob/master/maintainers/release-process.md)

- [`backport-*-to-*`](https://github.com/NixOS/nix/branches/all?query=backport)

  Generally branches created by the backport action.

  See [`maintainers/backporting.md`](https://github.com/NixOS/nix/blob/master/maintainers/backporting.md)

- [_other_](https://github.com/NixOS/nix/branches/all)

  Branches that do not conform to the above patterns should be feature branches.

## Reverting

If a change turns out to be merged by mistake, or contain a regression, it may be reverted.
A revert is not a rejection of the contribution, but merely part of an effective development process.
It makes sure that development keeps running smoothly, with minimal uncertainty, and less overhead.
If maintainers have to worry too much about avoiding reverts, they would not be able to merge as much.
By embracing reverts as a good part of the development process, everyone wins.

However, taking a step back may be frustrating, so maintainers will be extra supportive on the next try.
