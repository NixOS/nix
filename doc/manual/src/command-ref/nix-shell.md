# Name

`nix-shell` - start an interactive shell based on a Nix expression

# Synopsis

`nix-shell`
  [`--arg` *name* *value*]
  [`--argstr` *name* *value*]
  [{`--attr` | `-A`} *attrPath*]
  [`--command` *cmd*]
  [`--run` *cmd*]
  [`--exclude` *regexp*]
  [`--pure`]
  [`--keep` *name*]
  {{`--packages` | `-p`} {*packages* | *expressions*} … | [*path*]}

# Description

The command `nix-shell` will build the dependencies of the specified
derivation, but not the derivation itself. It will then start an
interactive shell in which all environment variables defined by the
derivation *path* have been set to their corresponding values, and the
script `$stdenv/setup` has been sourced. This is useful for reproducing
the environment of a derivation for development.

If *path* is not given, `nix-shell` defaults to `shell.nix` if it
exists, and `default.nix` otherwise.

If *path* starts with `http://` or `https://`, it is interpreted as the
URL of a tarball that will be downloaded and unpacked to a temporary
location. The tarball must include a single top-level directory
containing at least a file named `default.nix`.

If the derivation defines the variable `shellHook`, it will be run
after `$stdenv/setup` has been sourced. Since this hook is not executed
by regular Nix builds, it allows you to perform initialisation specific
to `nix-shell`. For example, the derivation attribute

```nix
shellHook =
  ''
    echo "Hello shell"
    export SOME_API_TOKEN="$(cat ~/.config/some-app/api-token)"
  '';
```

will cause `nix-shell` to print `Hello shell` and set the `SOME_API_TOKEN`
environment variable to a user-configured value.

# Options

All options not listed here are passed to `nix-store
--realise`, except for `--arg` and `--attr` / `-A` which are passed to
`nix-instantiate`.

  - `--command` *cmd*\
    In the environment of the derivation, run the shell command *cmd*.
    This command is executed in an interactive shell. (Use `--run` to
    use a non-interactive shell instead.) However, a call to `exit` is
    implicitly added to the command, so the shell will exit after
    running the command. To prevent this, add `return` at the end;
    e.g.  `--command "echo Hello; return"` will print `Hello` and then
    drop you into the interactive shell. This can be useful for doing
    any additional initialisation.

  - `--run` *cmd*\
    Like `--command`, but executes the command in a non-interactive
    shell. This means (among other things) that if you hit Ctrl-C while
    the command is running, the shell exits.

  - `--exclude` *regexp*\
    Do not build any dependencies whose store path matches the regular
    expression *regexp*. This option may be specified multiple times.

  - `--pure`\
    If this flag is specified, the environment is almost entirely
    cleared before the interactive shell is started, so you get an
    environment that more closely corresponds to the “real” Nix build. A
    few variables, in particular `HOME`, `USER` and `DISPLAY`, are
    retained.

  - `--packages` / `-p` *packages*…\
    Set up an environment in which the specified packages are present.
    The command line arguments are interpreted as attribute names inside
    the Nix Packages collection. Thus, `nix-shell -p libjpeg openjdk`
    will start a shell in which the packages denoted by the attribute
    names `libjpeg` and `openjdk` are present.

  - `-i` *interpreter*\
    The chained script interpreter to be invoked by `nix-shell`. Only
    applicable in `#!`-scripts (described below).

  - `--keep` *name*\
    When a `--pure` shell is started, keep the listed environment
    variables.

The following common options are supported:

# Environment variables

  - `NIX_BUILD_SHELL`\
    Shell used to start the interactive environment. Defaults to the
    `bash` found in `PATH`.

# Examples

To build the dependencies of the package Pan, and start an interactive
shell in which to build it:

```console
$ nix-shell '<nixpkgs>' -A pan
[nix-shell]$ eval ${unpackPhase:-unpackPhase}
[nix-shell]$ cd pan-*
[nix-shell]$ eval ${configurePhase:-configurePhase}
[nix-shell]$ eval ${buildPhase:-buildPhase}
[nix-shell]$ ./pan/gui/pan
```

The reason we use form `eval ${configurePhase:-configurePhase}` here is because
those packages that override these phases do so by exporting the overridden
values in the environment variable of the same name.
Here bash is being told to either evaluate the contents of 'configurePhase',
if it exists as a variable, otherwise evaluate the configurePhase function.

To clear the environment first, and do some additional automatic
initialisation of the interactive shell:

```console
$ nix-shell '<nixpkgs>' -A pan --pure \
    --command 'export NIX_DEBUG=1; export NIX_CORES=8; return'
```

Nix expressions can also be given on the command line using the `-E` and
`-p` flags. For instance, the following starts a shell containing the
packages `sqlite` and `libX11`:

```console
$ nix-shell -E 'with import <nixpkgs> { }; runCommand "dummy" { buildInputs = [ sqlite xorg.libX11 ]; } ""'
```

A shorter way to do the same is:

```console
$ nix-shell -p sqlite xorg.libX11
[nix-shell]$ echo $NIX_LDFLAGS
… -L/nix/store/j1zg5v…-sqlite-3.8.0.2/lib -L/nix/store/0gmcz9…-libX11-1.6.1/lib …
```

Note that `-p` accepts multiple full nix expressions that are valid in
the `buildInputs = [ ... ]` shown above, not only package names. So the
following is also legal:

```console
$ nix-shell -p sqlite 'git.override { withManual = false; }'
```

The `-p` flag looks up Nixpkgs in the Nix search path. You can override
it by passing `-I` or setting `NIX_PATH`. For example, the following
gives you a shell containing the Pan package from a specific revision of
Nixpkgs:

```console
$ nix-shell -p pan -I nixpkgs=https://github.com/NixOS/nixpkgs/archive/8a3eea054838b55aca962c3fbde9c83c102b8bf2.tar.gz

[nix-shell:~]$ pan --version
Pan 0.139
```

# Use as a `#!`-interpreter

You can use `nix-shell` as a script interpreter to allow scripts written
in arbitrary languages to obtain their own dependencies via Nix. This is
done by starting the script with the following lines:

```bash
#! /usr/bin/env nix-shell
#! nix-shell -i real-interpreter -p packages
```

where *real-interpreter* is the “real” script interpreter that will be
invoked by `nix-shell` after it has obtained the dependencies and
initialised the environment, and *packages* are the attribute names of
the dependencies in Nixpkgs.

The lines starting with `#! nix-shell` specify `nix-shell` options (see
above). Note that you cannot write `#! /usr/bin/env nix-shell -i ...`
because many operating systems only allow one argument in `#!` lines.

For example, here is a Python script that depends on Python and the
`prettytable` package:

```python
#! /usr/bin/env nix-shell
#! nix-shell -i python -p python pythonPackages.prettytable

import prettytable

# Print a simple table.
t = prettytable.PrettyTable(["N", "N^2"])
for n in range(1, 10): t.add_row([n, n * n])
print t
```

Similarly, the following is a Perl script that specifies that it
requires Perl and the `HTML::TokeParser::Simple` and `LWP` packages:

```perl
#! /usr/bin/env nix-shell
#! nix-shell -i perl -p perl perlPackages.HTMLTokeParserSimple perlPackages.LWP

use HTML::TokeParser::Simple;

# Fetch nixos.org and print all hrefs.
my $p = HTML::TokeParser::Simple->new(url => 'http://nixos.org/');

while (my $token = $p->get_tag("a")) {
    my $href = $token->get_attr("href");
    print "$href\n" if $href;
}
```

Sometimes you need to pass a simple Nix expression to customize a
package like Terraform:

```bash
#! /usr/bin/env nix-shell
#! nix-shell -i bash -p "terraform.withPlugins (plugins: [ plugins.openstack ])"

terraform apply
```

> **Note**
>
> You must use double quotes (`"`) when passing a simple Nix expression
> in a nix-shell shebang.

Finally, using the merging of multiple nix-shell shebangs the following
Haskell script uses a specific branch of Nixpkgs/NixOS (the 20.03 stable
branch):

```haskell
#! /usr/bin/env nix-shell
#! nix-shell -i runghc -p "haskellPackages.ghcWithPackages (ps: [ps.download-curl ps.tagsoup])"
#! nix-shell -I nixpkgs=https://github.com/NixOS/nixpkgs/archive/nixos-20.03.tar.gz

import Network.Curl.Download
import Text.HTML.TagSoup
import Data.Either
import Data.ByteString.Char8 (unpack)

-- Fetch nixos.org and print all hrefs.
main = do
  resp <- openURI "https://nixos.org/"
  let tags = filter (isTagOpenName "a") $ parseTags $ unpack $ fromRight undefined resp
  let tags' = map (fromAttrib "href") tags
  mapM_ putStrLn $ filter (/= "") tags'
```

If you want to be even more precise, you can specify a specific revision
of Nixpkgs:

    #! nix-shell -I nixpkgs=https://github.com/NixOS/nixpkgs/archive/0672315759b3e15e2121365f067c1c8c56bb4722.tar.gz

The examples above all used `-p` to get dependencies from Nixpkgs. You
can also use a Nix expression to build your own dependencies. For
example, the Python example could have been written as:

```python
#! /usr/bin/env nix-shell
#! nix-shell deps.nix -i python
```

where the file `deps.nix` in the same directory as the `#!`-script
contains:

```nix
with import <nixpkgs> {};

runCommand "dummy" { buildInputs = [ python pythonPackages.prettytable ]; } ""
```
