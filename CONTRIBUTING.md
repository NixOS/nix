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

1. Search for related issues that cover what you're going to work on.
   It could help to mention there that you will work on the issue.

   We strongly recommend first-time contributors not to propose new features but rather fix tightly-scoped problems in order to build trust and a working relationship with maintainers.

   Issues labeled [good first issue](https://github.com/NixOS/nix/labels/good%20first%20issue) should be relatively easy to fix and are likely to get merged quickly.
   Pull requests addressing issues labeled [idea approved](https://github.com/NixOS/nix/labels/idea%20approved) or [RFC](https://github.com/NixOS/nix/labels/RFC) are especially welcomed by maintainers and will receive prioritised review.

   If you are proficient with C++, addressing one of the [popular issues](https://github.com/NixOS/nix/issues?q=is%3Aissue+is%3Aopen+sort%3Areactions-%2B1-desc) will be highly appreciated by maintainers and Nix users all over the world.
   For far-reaching changes, please investigate possible blockers and design implications, and coordinate with maintainers before investing too much time in writing code that may not end up getting merged.

   If there is no relevant issue yet and you're not sure whether your change is likely to be accepted, [open an issue](https://github.com/NixOS/nix/issues/new/choose) yourself.

2. Check for [pull requests](https://github.com/NixOS/nix/pulls) that might already cover the contribution you are about to make.
   There are many open pull requests that might already do what you intend to work on.
   You can use [labels](https://github.com/NixOS/nix/labels) to filter for relevant topics.

3. Check the [Nix reference manual](https://nix.dev/manual/nix/development/development/building.html) for information on building Nix and running its tests.

   For contributions to the command line interface, please check the [CLI guidelines](https://nix.dev/manual/nix/development/development/cli-guideline.html).

4. Make your change!

5. [Create a pull request](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/creating-a-pull-request) for your changes.
   * Clearly explain the problem that you're solving.

     Link related issues to inform interested parties and future contributors about your change.
     If your pull request closes one or multiple issues, mention that in the description using `Closes: #<number>`, as it will then happen automatically when your change is merged.
   * Credit original authors when you're reusing or building on their work.
   * Link to relevant changes in other projects, so that others can understand the full context of the change in the future when you or someone else will change or troubleshoot the code.
     This is especially important when your change is based on work done in other repositories.

     Example:
     ```
     This is based on the work of @user in <url>.
     This solution took inspiration from <url>.

     Co-authored-by: User Name <user@example.com>
     ```

     When cherry-picking from a different repository, use the `-x` flag, and then amend the commits to turn the hashes into URLs.

   * Make sure to have [a clean history of commits on your branch by using rebase](https://www.digitalocean.com/community/tutorials/how-to-rebase-and-update-a-pull-request).
   * [Mark the pull request as draft](https://docs.github.com/en/pull-requests/collaborating-with-pull-requests/proposing-changes-to-your-work-with-pull-requests/changing-the-stage-of-a-pull-request) if you're not done with the changes.

6. Do not expect your pull request to be reviewed immediately.
   Nix maintainers follow a [structured process for reviews and design decisions](https://github.com/NixOS/nix/tree/master/maintainers#project-board-protocol), which may or may not prioritise your work.

   Following this checklist will make the process smoother for everyone:

   - [ ] Fixes an [idea approved](https://github.com/NixOS/nix/labels/idea%20approved) issue
   - [ ] Tests, as appropriate:
     - Functional tests – [`tests/functional/**.sh`](./tests/functional)
     - Unit tests – [`src/*/tests`](./src/)
     - Integration tests – [`tests/nixos/*`](./tests/nixos)
   - [ ] User documentation in the [manual](./doc/manual/source)
   - [ ] API documentation in header files
   - [ ] Code and comments are self-explanatory
   - [ ] Commit message explains **why** the change was made
   - [ ] New feature or incompatible change: [add a release note](https://nix.dev/manual/nix/development/development/contributing.html#add-a-release-note)

7. If you need additional feedback or help to getting pull request into shape, ask other contributors using [@mentions](https://docs.github.com/en/get-started/writing-on-github/getting-started-with-writing-and-formatting-on-github/basic-writing-and-formatting-syntax#mentioning-people-and-teams).

## Making changes to the Nix manual

The Nix reference manual is hosted on https://nix.dev/manual/nix.
The underlying source files are located in [`doc/manual/source`](./doc/manual/source).
For small changes you can [use GitHub to edit these files](https://docs.github.com/en/repositories/working-with-files/managing-files/editing-files)
For larger changes see the [Nix reference manual](https://nix.dev/manual/nix/development/development/contributing.html).

## Getting help

Whenever you're stuck or do not know how to proceed, you can always ask for help.
We invite you to use our [Matrix room](https://matrix.to/#/#nix-dev:nixos.org) to ask questions.
