# CLI guideline

## Goals

Purpose of this document is to provide a clear direction to **help design
delightful command line** experience. This document contains guidelines to
follow to ensure a consistent and approachable user experience.

## Overview

`nix` command provides a single entry to a number of sub-commands that help
**developers and system administrators** in the life-cycle of a software
project. We particularly need to pay special attention to help and assist new
users of Nix.

# Naming the `COMMANDS`

Words matter. Naming is an important part of the usability. Users will be
interacting with Nix on a regular basis so we should **name things for ease of
understanding**.

We recommend following the [Principle of Least
Astonishment](https://en.wikipedia.org/wiki/Principle_of_least_astonishment).
This means that you should **never use acronyms or abbreviations** unless they
are commonly used in other tools (e.g. `nix init`). And if the command name is
too long (> 10-12 characters) then shortening it makes sense (e.g.
“prioritization” → “priority”).

Commands should **follow a noun-verb dialogue**. Although noun-verb formatting
seems backwards from a speaking perspective (i.e. `nix store copy` vs. `nix
copy store`) it allows us to organize commands the same way users think about
completing an action (the group first, then the command).

## Naming rules

Rules are there to guide you by limiting your options. But not everything can
fit the rules all the time. In those cases document the exceptions in [Appendix
1: Commands naming exceptions](#appendix-1-commands-naming-exceptions) and
provide reason. The rules want to force a Nix developer to look, not just at
the command at hand, but also the command in a full context alongside other
`nix` commands.

```shell
$ nix [<GROUP>] <COMMAND> [<ARGUMENTS>] [<OPTIONS>]
```

- `GROUP`, `COMMAND`, `ARGUMENTS` and `OPTIONS` should be lowercase and in a
  singular form.
- `GROUP` should be a  **NOUN**.
- `COMMAND` should be a **VERB**.
- `ARGUMENTS` and `OPTIONS` are discussed in [*Input* section](#input).

## Classification

Some commands are more important, some less. While we want all of our commands
to be perfect we can only spend limited amount of time testing and improving
them.

This classification tries to separate commands in 3 categories in terms of
their importance in regards to the new users. Users who are likely to be
impacted the most by bad user experience.

- **Main commands**

  Commands used for our main use cases and most likely used by new users. We
  expect attention to details, such as:

    - Proper use of [colors](#colors), [emojis](#special-unicode-characters)
      and [aligning of text](#text-alignment).
    - [Autocomplete](#shell-completion) of options.
    - Show [next possible steps](#next-steps).
    - Showing some [“tips”](#educate-the-user) when running logs running tasks
      (eg. building / downloading) in order to teach users interesting bits of
      Nix ecosystem.
    - [Help pages](#help-is-essential) to be as good as we can write them
      pointing to external documentation and tutorials for more.

  Examples of such commands: `nix init`, `nix develop`, `nix build`, `nix run`,
  ...

- **Infrequently used commands**

  From infrequently used commands we expect less attention to details, but
  still some:

    - Proper use of [colors](#colors), [emojis](#special-unicode-characters)
      and [aligning of text](#text-alignment).
    - [Autocomplete](#shell-completion) of options.

  Examples of such commands: `nix doctor`, `nix edit`, `nix eval`, ...

- **Utility and scripting commands**

  Commands that expose certain internal functionality of `nix`, mostly used by
  other scripts.

    - [Autocomplete](#shell-completion) of options.

  Examples of such commands: `nix store copy`, `nix hash base16`, `nix store
  ping`, ...


# Help is essential

Help should be built into your command line so that new users can gradually
discover new features when they need them. 

## Looking for help

Since there is no standard way how user will look for help we rely on ways help
is provided by commonly used tools. As a guide for this we took `git` and
whenever in doubt look at it as a preferred direction.

The rules are:

- Help is shown by using `--help` or `help` command (eg `nix` `--``help` or
  `nix help`).
- For non-COMMANDs (eg. `nix` `--``help` and `nix store` `--``help`) we **show
  a summary** of most common use cases. Summary is presented on the STDOUT
  without any use of PAGER.
- For COMMANDs (eg. `nix init` `--``help` or `nix help init`) we display the
  man page of that command. By default the PAGER is used (as in `git`).
- At the end of either summary or man page there should be an URL pointing to
  an online version of more detailed documentation.
- The structure of summaries and man pages should be the same as in `git`.

## Anticipate where help is needed

Even better then requiring the user to search for help is to anticipate and
predict when user might need it. Either because the lack of discoverability,
typo in the input or simply taking the opportunity to teach the user of
interesting - but less visible - details.

### Shell completion

This type of help is most common and almost expected by users. We need to
**provide the best shell completion** for `bash`, `zsh` and `fish`.

Completion needs to be **context aware**, this mean when a user types:

```shell
$ nix build n<TAB>
```

we need to display a list of flakes starting with `n`.

### Wrong input

As we all know we humans make mistakes, all the time. When a typo - intentional
or unintentional - is made, we should prompt for closest possible options or
point to the documentation which would educate user to not make the same
errors. Here are few examples:

In first example we prompt the user for typing wrong command name:


```shell
$ nix int
------------------------------------------------------------------------
  Error! Command `int` not found.
------------------------------------------------------------------------
  Did you mean:
    |> nix init
    |> nix input
```

Sometimes users will make mistake either because of a typo or simply because of
lack of discoverability. Our handling of this cases needs to be context
sensitive.


```shell
$ nix init --template=template#pyton
------------------------------------------------------------------------
  Error! Template `template#pyton` not found.
------------------------------------------------------------------------
Initializing Nix project at `/path/to/here`.
      Select a template for you new project:
          |> template#pyton
             template#python-pip
             template#python-poetry
```

### Next steps

It can be invaluable to newcomers to show what a possible next steps and what
is the usual development workflow with Nix. For example:


```shell
$ nix init --template=template#python
Initializing project `template#python`
          in `/home/USER/dev/new-project`

  Next steps
    |> nix develop   -- to enter development environment
    |> nix build     -- to build your project
```

### Educate the user

We should take any opportunity to **educate users**, but at the same time we
must **be very very careful to not annoy users**. There is a thin line between
being helpful and being annoying.

An example of educating users might be to provide *Tips* in places where they
are waiting.

```shell
$ nix build
    Started building my-project 1.2.3
 Downloaded python3.8-poetry 1.2.3 in 5.3 seconds
 Downloaded python3.8-requests 1.2.3 in 5.3 seconds
------------------------------------------------------------------------
      Press `v` to increase logs verbosity
         |> `?` to see other options
------------------------------------------------------------------------
      Learn something new with every build...
         |> See last logs of a build with `nix log --last` command.
------------------------------------------------------------------------
  Evaluated my-project 1.2.3 in 14.43 seconds
Downloading [12 / 200]
         |> firefox 1.2.3 [#########>       ] 10Mb/s | 2min left
   Building [2 / 20]
         |> glibc 1.2.3 -> buildPhase: <last log line>
------------------------------------------------------------------------
```

Now **Learn** part of the output is where you educate users. You should only
show it when you know that a build will take some time and not annoy users of
the builds that take only few seconds.

Every feature like this should go through an intensive review and testing to
collect as much feedback as possible and to fine tune every little detail. If
done right this can be an awesome features beginners and advance users will
love, but if not done perfectly it will annoy users and leave bad impression.

# Input

Input to a command is provided via `ARGUMENTS` and `OPTIONS`. 

`ARGUMENTS` represent a required input for a function. When choosing to use
`ARGUMENT` over function please be aware of the downsides that come with it:

- User will need to remember the order of `ARGUMENTS`. This is not a problem if
  there is only one `ARGUMENT`.
- With `OPTIONS` it is possible to provide much better auto completion.
- With `OPTIONS` it is possible to provide much better error message.
- Using `OPTIONS` it will mean there is a little bit more typing.

We don’t discourage the use of `ARGUMENTS`, but simply want to make every
developer consider the downsides and choose wisely.

## Naming the `OPTIONS`

Then only naming convention - apart from the ones mentioned in Naming the
`COMMANDS` section is how flags are named.

Flags are a type of `OPTION` that represent an option that can be turned ON of
OFF. We can say **flags are boolean type of** `**OPTION**`.

Here are few examples of flag `OPTIONS`:

- `--colors` vs. `--no-colors` (showing colors in the output)
- `--emojis` vs. `--no-emojis` (showing emojis in the output)

## Prompt when input not provided

For *main commands* (as [per classification](#classification)) we want command
to improve the discoverability of possible input. A new user will most likely
not know which `ARGUMENTS` and `OPTIONS` are required or which values are
possible for those options.

In cases, the user might not provide the input or they provide wrong input,
rather than show the error, prompt a user with an option to find and select
correct input (see examples).

Prompting is of course not required when TTY is not attached to STDIN. This
would mean that scripts won't need to handle prompt, but rather handle errors.

A place to use prompt and provide user with interactive select


```shell
$ nix init
Initializing Nix project at `/path/to/here`.
      Select a template for you new project:
          |> py
             template#python-pip
             template#python-poetry
             [ Showing 2 templates from 1345 templates ]
```

Another great place to add prompts are **confirmation dialogues for dangerous
actions**. For example when adding new substitutor via `OPTIONS` or via
`flake.nix` we should prompt - for the first time - and let user review what is
going to happen.


```shell
$ nix build --option substitutors https://cache.example.org
------------------------------------------------------------------------
  Warning! A security related question needs to be answered.
------------------------------------------------------------------------
  The following substitutors will be used to in `my-project`: 
    - https://cache.example.org

  Do you allow `my-project` to use above mentioned substitutors?
    [y/N] |> y
```

# Output

Terminal output can be quite limiting in many ways. Which should force us to
think about the experience even more. As with every design the output is a
compromise between being terse and being verbose, between showing help to
beginners and annoying advance users. For this it is important that we know
what are the priorities.

Nix command line should be first and foremost written with beginners in mind.
But users won't stay beginners for long and what was once useful might quickly
become annoying. There is no golden rule that we can give in this guideline
that would make it easier how to draw a line and find best compromise.

What we would encourage is to **build prototypes**, do some **user testing**
and collect **feedback**. Then repeat the cycle few times.

First design the *happy path* and only after your iron it out, continue to work
on **edge cases** (handling and displaying errors, changes of the output by
certain `OPTIONS`, etc…)

## Follow best practices

Needless to say we Nix must be a good citizen and follow best practices in
command line.

In short: **STDOUT is for output, STDERR is for (human) messaging.**

STDOUT and STDERR provide a way for you to output messages to the user while
also allowing them to redirect content to a file. For example:

```shell
$ nix build > build.txt
------------------------------------------------------------------------
  Error! Atrribute `bin` missing at (1:94) from string.
------------------------------------------------------------------------

  1| with import <nixpkgs> { }; (pkgs.runCommandCC or pkgs.runCommand) "shell" { buildInputs = [ (surge.bin) ]; } ""
```

Because this warning is on STDERR, it doesn’t end up in the file.

But not everything on STDERR is an error though. For example, you can run `nix
build` and collect logs in a file while still seeing the progress.

```
$ nix build > build.txt
  Evaluated 1234 files in 1.2 seconds
 Downloaded python3.8-poetry 1.2.3 in 5.3 seconds
 Downloaded python3.8-requests 1.2.3 in 5.3 seconds
------------------------------------------------------------------------
      Press `v` to increase logs verbosity
         |> `?` to see other options
------------------------------------------------------------------------
      Learn something new with every build...
         |> See last logs of a build with `nix log --last` command.
------------------------------------------------------------------------
  Evaluated my-project 1.2.3 in 14.43 seconds
Downloading [12 / 200]
         |> firefox 1.2.3 [#########>       ] 10Mb/s | 2min left
   Building [2 / 20]
         |> glibc 1.2.3 -> buildPhase: <last log line>
------------------------------------------------------------------------
```

## Errors (WIP)

**TODO**: Once we have implementation for the *happy path* then we will think
how to present errors.

## Not only for humans

Terse, machine-readable output formats can also be useful but shouldn’t get in
the way of making beautiful CLI output. When needed, commands should offer a
`--json` flag to allow users to easily parse and script the CLI.

When TTY is not detected on STDOUT we should remove all design elements (no
colors, no emojis and using ASCII instead of Unicode symbols). The same should
happen when TTY is not detected on STDERR. We should not display progress /
status section, but only print warnings and errors.

## Dialog with the user

CLIs don't always make it clear when an action has taken place. For every
action a user performs, your CLI should provide an equal and appropriate
reaction, clearly highlighting the what just happened. For example:

```shell
$ nix build
 Downloaded python3.8-poetry 1.2.3 in 5.3 seconds
 Downloaded python3.8-requests 1.2.3 in 5.3 seconds
...
   Success! You have successfully built my-project.
$
```

Above command clearly states that command successfully completed. And in case
of `nix build`, which is a command that might take some time to complete, it is
equally important to also show that a command started.

## Text alignment 

Text alignment is the number one design element that will present all of the
Nix commands as a family and not as separate tools glued together.

The format we should follow is:

```shell
$ nix COMMAND
   VERB_1 NOUN and other words
  VERB__1 NOUN and other words
       |> Some details 
```

Few rules that we can extract from above example:

- Each line should start at least with one space.
- First word should be a VERB and must be aligned to the right.
- Second word should be a NOUN and must be aligned to the left.
- If you can not find a good VERB / NOUN pair, don’t worry make it as
  understandable to the user as possible.
- More details of each line can be provided by `|>` character which is serving
  as the first word when aligning the text

Don’t forget you should also test your terminal output with colors and emojis
off (`--no-colors --no-emojis`).

## Dim / Bright

After comparing few terminals with different color schemes we would **recommend
to avoid using dimmed text**. The difference from the rest of the text is very
little in many terminal and color scheme combinations. Sometimes the difference
is not even notable, therefore relying on it wouldn’t make much sense.

**The bright text is much better supported** across terminals and color
schemes. Most of the time the difference is perceived as if the bright text
would be bold. 

## Colors

Humans are already conditioned by society to attach certain meaning to certain
colors. While the meaning is not universal, a simple collection of colors is
used to represent basic emotions. 

Colors that can be used in output

- Red = error, danger, stop
- Green = success, good
- Yellow/Orange = proceed with caution, warning, in progress
- Blue/Magenta = stability, calm

While colors are nice, when command line is used by machines (in automation
scripts) you want to remove the colors. There should be a global `--no-colors`
option that would remove the colors.

## Special (Unicode) characters

Most of the terminal have good support for Unicode characters and you should
use them in your output by default. But always have a backup solution that is
implemented only with ASCII characters and will be used when `--ascii` option
is going to be passed in. Please make sure that you test your output also
without Unicode characters

More they showing all the different Unicode characters it is important to
**establish common set of characters** that we use for certain situations.

## Emojis

Emojis help channel emotions even better than text, colors and special
characters.

We recommend **keeping the set of emojis to a minimum**. This will enable each
emoji to stand out more.

As not everybody is happy about emojis we should provide an `--no-emojis`
option to disable them. Please make sure that you test your output also without
emojis.

## Tables

All commands that are listing certain data can be implemented in some sort of a
table. It’s important that each row of your output is a single ‘entry’ of data.
Never output table borders. It’s noisy and a huge pain for parsing using other
tools such as `grep`.

Be mindful of the screen width. Only show a few columns by default with the
table header, for more the table can be manipulated by the following options:

- `--no-headers`: Show column headers by default but allow to hide them.
- `--columns`: Comma-separated list of column names to add.
- `--sort`: Allow sorting by column. Allow inverse and multi-column sort as well.

## Interactive output

Interactive output was selected to be able to strike the balance between
beginners and advance users. While the default output will target beginners it
can, with a few key strokes, be changed into and advance introspection tool.

### Progress

For longer running commands we should provide and overview the progress.
This is shown best in `nix build` example:

```shell
$ nix build
    Started building my-project 1.2.3
 Downloaded python3.8-poetry 1.2.3 in 5.3 seconds
 Downloaded python3.8-requests 1.2.3 in 5.3 seconds
------------------------------------------------------------------------
      Press `v` to increase logs verbosity
         |> `?` to see other options
------------------------------------------------------------------------
      Learn something new with every build...
         |> See last logs of a build with `nix log --last` command.
------------------------------------------------------------------------
  Evaluated my-project 1.2.3 in 14.43 seconds
Downloading [12 / 200]
         |> firefox 1.2.3 [#########>       ] 10Mb/s | 2min left
   Building [2 / 20]
         |> glibc 1.2.3 -> buildPhase: <last log line>
------------------------------------------------------------------------
```

### Search

Use a `fzf` like fuzzy search when there are multiple options to choose from.

```shell
$ nix init
Initializing Nix project at `/path/to/here`.
      Select a template for you new project:
          |> py
             template#python-pip
             template#python-poetry
             [ Showing 2 templates from 1345 templates ]
```

### Prompt

In some situations we need to prompt the user and inform the user about what is
going to happen.

```shell
$ nix build --option substitutors https://cache.example.org
------------------------------------------------------------------------
  Warning! A security related question needs to be answered.
------------------------------------------------------------------------
  The following substitutors will be used to in `my-project`: 
    - https://cache.example.org

  Do you allow `my-project` to use above mentioned substitutors?
    [y/N] |> y
```

## Verbosity

There are many ways that you can control verbosity.

Verbosity levels are: 

- `ERROR` (level 0)
- `WARN` (level 1)
- `NOTICE` (level 2)
- `INFO` (level 3)
- `TALKATIVE` (level 4)
- `CHATTY` (level 5)
- `DEBUG` (level 6)
- `VOMIT` (level 7)

The default level that the command starts is `ERROR`. The simplest way to
increase the verbosity by stacking `-v` option (eg: `-vvv == level 3 == INFO`).
There are also two shortcuts, `--debug` to run in `DEBUG` verbosity level and
`--quiet` to run in `ERROR` verbosity level.

----------

# Appendix 1: Commands naming exceptions

`nix init` and `nix repl` are well established 
