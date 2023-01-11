# Nix release process

## Release artifacts

The release process is intended to create the following for each
release:

* A Git tag

* Binary tarballs in https://releases.nixos.org/?prefix=nix/

* Docker images

* Closures in https://cache.nixos.org

* (Optionally) Updated `fallback-paths.nix` in Nixpkgs

* An updated manual on https://nixos.org/manual/nix/stable/

## Creating a new release from the `master` branch

* Make sure that the [Hydra `master` jobset](https://hydra.nixos.org/jobset/nix/master) succeeds.

* In a checkout of the Nix repo, make sure you're on `master` and run
  `git pull`.

* Move the contents of `doc/manual/src/release-notes/rl-next.md`
  (except the first line) to
  `doc/manual/src/release-notes/rl-$VERSION.md` (where `$VERSION` is
  the contents of `.version` *without* the patch level, e.g. `2.12`
  rather than `2.12.0`).

* Add a header to `doc/manual/src/release-notes/rl-$VERSION.md` like

  ```
  # Release 2.12 (2022-12-06)
  ```

* Proof-read / edit / rearrange the release notes. Breaking changes
  and highlights should go to the top.

* Add a link to the release notes to `doc/manual/src/SUMMARY.md.in`
  (*not* `SUMMARY.md`), e.g.

  ```
  - [Release 2.12 (2022-12-06)](release-notes/rl-2.12.md)
  ```

* Run

  ```console
  $ git checkout -b release-notes
  $ git add doc/manual/src/release-notes/rl-$VERSION.md
  $ git commit -a -m 'Release notes'
  $ git push --set-upstream $REMOTE release-notes
  ```

* Create a PR for `release-notes`.

* Wait for the PR to be merged.

* Create a branch for the release:

  ```console
  $ git checkout master
  $ git pull
  $ git checkout -b $VERSION-maintenance
  ```

* Mark the release as stable:

  ```console
  $ git cherry-pick f673551e71942a52b6d7ae66af8b67140904a76a
  ```

  This removes the link to `rl-next.md` from the manual and sets
  `officialRelease = true` in `flake.nix`.

* Push the release branch:

  ```console
  $ git push --set-upstream origin $VERSION-maintenance
  ```

* Create a jobset for the release branch on Hydra as follows:

  * Go to the jobset of the previous release
  (e.g. https://hydra.nixos.org/jobset/nix/maintenance-2.11).

  * Select `Actions -> Clone this jobset`.

  * Set identifier to `maintenance-$VERSION`.

  * Set description to `$VERSION release branch`.

  * Set flake URL to `github:NixOS/nix/$VERSION-maintenance`.

  * Hit `Create jobset`.

* Wait for the new jobset to evaluate and build. If impatient, go to
  the evaluation and select `Actions -> Bump builds to front of
  queue`.

* When the jobset evaluation has succeeded building, take note of the
  evaluation ID (e.g. `1780832` in
  `https://hydra.nixos.org/eval/1780832`).

* Tag the release and upload the release artifacts to
  [`releases.nixos.org`](https://releases.nixos.org/) and [Docker Hub](https://hub.docker.com/):

  ```console
  $ IS_LATEST=1 ./maintainers/upload-release.pl <EVAL-ID>
  ```

  Note: `IS_LATEST=1` causes the `latest-release` branch to be
  force-updated. This is used by the `nixos.org` website to get the
  [latest Nix manual](https://nixos.org/manual/nixpkgs/unstable/).

  TODO: This script requires the right AWS credentials. Document.

  TODO: This script currently requires a
  `/home/eelco/Dev/nix-pristine` and
  `/home/eelco/Dev/nixpkgs-pristine`.

  TODO: trigger nixos.org netlify: https://docs.netlify.com/configure-builds/build-hooks/
* Prepare for the next point release by editing `.version` to
  e.g.

  ```console
  $ echo 2.12.1 > .version
  $ git commit -a -m 'Bump version'
  $ git push
  ```

  Commit and push this to the maintenance branch.

* Bump the version of `master`:

  ```console
  $ git checkout master
  $ git pull
  $ NEW_VERSION=2.13.0
  $ echo -n $NEW_VERSION > .version
  $ git checkout -b bump-$NEW_VERSION
  $ git commit -a -m 'Bump version'
  $ git push --set-upstream origin bump-$NEW_VERSION
  ```

  Make a pull request and auto-merge it.

* Create a milestone for the next release, move all unresolved issues
  from the previous milestone, and close the previous milestone. Set
  the date for the next milestone 6 weeks from now.

* Create a backport label

* Post an [announcement on Discourse](https://discourse.nixos.org/c/announcements/8), including the contents of
  `rl-$VERSION.md`.

## Creating a point release

* Wait for the desired evaluation of the maintenance jobset to finish
  building.

* Run

  ```console
  $ IS_LATEST=1 ./maintainers/upload-release.pl <EVAL-ID>
  ```

  Omit `IS_LATEST=1` when creating a point release that is not on the
  most recent stable branch. This prevents `nixos.org` to going back
  to an older release.

* Bump the version number of the release branch as above (e.g. to
  `2.12.2`).
  
## Recovering from mistakes

`upload-release.pl` should be idempotent. For instance a wrong `IS_LATEST` value can be fixed that way, by running the script on the actual latest release.

