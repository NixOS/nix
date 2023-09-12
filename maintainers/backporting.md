
# Backporting

To [automatically backport a pull request](https://github.com/NixOS/nix/blob/master/.github/workflows/backport.yml) to a release branch once it's merged, assign it a label of the form [`backport <branch>`](https://github.com/NixOS/nix/labels?q=backport).

Since [GitHub Actions workflows will not trigger other workflows](https://docs.github.com/en/actions/using-workflows/triggering-a-workflow#triggering-a-workflow-from-a-workflow), checks on the automatic backport need to be triggered by another actor.
This is achieved by closing and reopening the backport pull request.

This specifically affects the [`installer_test`] check.
Note that it only runs after the other tests, so it may take a while to appear.

[`installer_test`]: https://github.com/NixOS/nix/blob/895dfc656a21f6252ddf48df0d1f215effa04ecb/.github/workflows/ci.yml#L70-L91
