# Contributing to Nix

Welcome and thank you for your interest in contributing to Nix!
We appreciate your support.

Reading and following these guidelines will help us make the contribution process easy and effective for everyone involved.


## Report a bug

1. Check on the [GitHub issue tracker](https://github.com/NixOS/nix/issues) if your bug was already reported.

2. If you were not able to find the bug or feature [open a new issue](https://github.com/NixOS/nix/issues/new/choose)

3. The issue templates will guide you in specifying your issue.
   The more complete the information you provide, the more likely it can be found by others and the more useful it is in the future.
   Make sure reported bugs can be reproduced easily.

4. Once submitted, do not expect issues to be picked up or solved right away.
   The only way to ensure this, is to [work on the issue yourself](#making-changes-to-nix).

## Report a security vulnerability

Check out the [security policy](https://github.com/NixOS/nix/security/policy).

## Making changes to Nix

1. Check for [pull requests](https://github.com/NixOS/nix/pulls) that might already cover the contribution you are about to make.
   There are many open pull requests that might already do what you intent to work on.
   You can use [labels](https://github.com/NixOS/nix/labels) to filter for relevant topics.

2. Search for related issues that cover what you're going to work on. It could help to mention there that you will work on the issue.
   Pull requests addressing issues labeled ["idea approved"](https://github.com/NixOS/nix/labels/idea%20approved) are especially welcomed by maintainers and will receive prioritised review.

3. Check the [Nix reference manual](https://nixos.org/manual/nix/unstable/contributing/hacking.html) for information on building Nix and running its tests.

   For contributions to the command line interface, please check the [CLI guidelines](https://nixos.org/manual/nix/unstable/contributing/cli-guideline.html).

4. Make your changes!

5. [Create a pull request](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request) for your changes.
   * [Mark the pull request as draft](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/changing-the-stage-of-a-pull-request) if you're not done with the changes.
   * Make sure to have [a clean history of commits on your branch by using rebase](https://www.digitalocean.com/community/tutorials/how-to-rebase-and-update-a-pull-request).
   * Link related issues in your pull request to inform interested parties and future contributors about your change.
     If your pull request closes one or multiple issues, note that in the description using `Closes: #<number>`, as it will then happen automatically when your change is merged.

6. Do not expect your pull request to be reviewed immediately.
   Nix maintainers follow a [structured process for reviews and design decisions](https://github.com/NixOS/nix/tree/master/maintainers#project-board-protocol), which may or may not prioritise your work.

7. If you need additional feedback or help to getting pull request into shape, ask other contributors using [@mentions](https://docs.github.com/en/get-started/writing-on-github/getting-started-with-writing-and-formatting-on-github/basic-writing-and-formatting-syntax#mentioning-people-and-teams).

## Making changes to the Nix manual

The Nix reference manual is hosted on https://nixos.org/manual/nix.
The underlying source files are located in [`doc/manual/src`](./doc/manual/src).
For small changes you can [use GitHub to edit these files](https://docs.github.com/en/repositories/working-with-files/managing-files/editing-files)
For larger changes see the [Nix reference manual](https://nixos.org/manual/nix/unstable/contributing/hacking.html).

## Getting help

Whenever you're stuck or do not know how to proceed, you can always ask for help.
The appropriate channels to do so can be found on the [NixOS Community](https://nixos.org/community/) page.
