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
   * Review the **Automation/AI Policy** below.

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

## Automation/AI policy

Every contribution to Nix and related development venues, including code, documentation, and communication on GitHub and Matrix, must have a responsible person in the loop who is accountable for that contribution and reviews it before submission, and must transparently disclose any non‐trivial use of automation to produce it, including but not limited to LLM‐based AI tools.

Human communication must remain human. Pull request / issue descriptions and comments, documentation, commit messages, and code comments must be human-authored.

The following sections give more detail.

### Scope

Any use of automated tools to generate non‐trivial amounts of output as part of a contribution, in whole or in part, verbatim or edited, is covered by this policy, except as listed in the Exemptions section.
Both LLM‐based AI tools and hand‐written automation are covered.
Contributions include code and documentation in commits, commit messages, pull request summaries and reviews, issue and vulnerability reports, GitHub comments, Matrix messages, and Discourse posts.
The covered venues are the GitHub repositories for Nix and related projects under the jurisdiction of the Nix team, Matrix rooms that are focused on development of those projects, and Discourse topics about Nix development.

PRs that seek to address issues that appear easier to fix, such as those marked with [good first issue](https://github.com/NixOS/nix/labels/good%20first%20issue), are held to the same standard as other issues.
Just because the problem seems "easy" and more likely to be successfully fixed by an unsupervised agent does not mean bending the rules is permissible.
Even the most trivial changes still must have a responsible person in the loop.

### Accountability

Everyone who submits a contribution to Nix is responsible for it, regardless of the use of automated tooling.
Before submission, they must establish a reasonable level of understanding of the contribution and expectation of its correctness.
A contributor submitting a contribution intended for inclusion in Nix is also responsible for ensuring that it is [appropriately licensed](https://github.com/NixOS/nix/blob/master/COPYING) and credited, and not encumbered by any incompatible copyright.

When output from automated tooling is used in contributions, a contributor must establish confidence in that output.
This can be achieved by establishing confidence in the correctness of the tooling’s logic, manual review of the included output, or using further automation to verify the output (e.g. regression tests that surface the issue before a bugfix commit).
As the inner workings of LLM‐based AI tools cannot be sufficiently understood at present, only the latter two options are available when those are used; vibe coding without review is not permitted.

This policy applies equally to any further discussion of a contribution.
Comments and reviews must separately satisfy the same requirements of understanding, review, and disclosure.
Contributors are expected to be able to answer questions about their contribution and respond to feedback appropriately, **without simply forwarding messages back and forth to automated tools**.

It is not permitted to submit automated contributions without any manual review or intervention, outside of standard community automation.
Automation without any manual review must not be used as the sole arbiter of whether to merge a change.

### Transparency

All covered use of automated tooling for a contribution must be disclosed as part of that contribution.

In the case of LLM‐based AI tooling used for commits, this **must** be in the form of an `Assisted-by:` Git commit trailer, including at least the tool name and the primary model name and version used for the contribution. When using unreleased models, it is acceptable to say "unspecified".
A `Co-authored-by:` trailer does not satisfy this policy.

Any adequate form of disclosure is permitted for other kinds of tooling and contribution.
Pull request summaries and review comments must be disclosed separately to commits.

### Exemptions

The following situations are fully or partially exempt:

* Use of standard deterministic editor/IDE/formatter/text transformation tooling to produce changes that the author manually reviews and understands is exempt, including inline "auto‐completion" (even if LLM‐based) of short, rote snippets of text that do not contribute anything beyond boilerplate the author would have written anyway, and spelling and grammar checkers.

* Use of standard community automation is exempt (e.g. dependabot).

* Use of AI tools for research, testing, debugging, or review is out of scope, if no substantial amount of their output is included in the resulting contribution.
  However, if these tools had a significant technical influence on your contribution, you are still responsible for it per the Accountability section, and are expected to disclose this where relevant.

* Use of machine translation for commit and pull request descriptions and comments is exempt from the requirement to understand the translated output.
  However, the requirements of appropriate confidence in the original text, responsibility, and disclosure still apply, and you are encouraged to additionally include the original untranslated contribution.

* Use of automation in a contribution clearly marked as not being ready for merge (e.g. a draft pull request) is exempt from the requirement for full self‐review, as long as some amount of review has been done and it is expected that the requirements will be met by the time it is marked as ready.
  This does not waive any other requirement.

## Making changes to the Nix manual

The Nix reference manual is hosted on https://nix.dev/manual/nix.
The underlying source files are located in [`doc/manual/source`](./doc/manual/source).
For small changes you can [use GitHub to edit these files](https://docs.github.com/en/repositories/working-with-files/managing-files/editing-files)
For larger changes see the [Nix reference manual](https://nix.dev/manual/nix/development/development/contributing.html).

You're encouraged to add line breaks at semantic boundaries, per [sembr](https://sembr.org).

## Getting help

Whenever you're stuck or do not know how to proceed, you can always ask for help.
We invite you to use our [Matrix room](https://matrix.to/#/#nix-dev:nixos.org) to ask questions.
