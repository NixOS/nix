R""(

# Examples

* Start a shell providing `youtube-dl` from the `nixpkgs` flake:

  ```console
  # nix shell nixpkgs#youtube-dl
  # youtube-dl --version
  2020.11.01.1
  ```

* Start a shell providing GNU Hello from NixOS 20.03:

  ```console
  # nix shell nixpkgs/nixos-20.03#hello
  ```

* Run GNU Hello:

  ```console
  # nix shell nixpkgs#hello --command hello --greeting 'Hi everybody!'
  Hi everybody!
  ```

* Run multiple commands in a shell environment:

  ```console
  # nix shell nixpkgs#gnumake --command sh -c "cd src && make"
  ```

* Run GNU Hello in a chroot store:

  ```console
  # nix shell --store ~/my-nix nixpkgs#hello --command hello
  ```

* Start a shell providing GNU Hello in a chroot store:

  ```console
  # nix shell --store ~/my-nix nixpkgs#hello nixpkgs#bashInteractive --command bash
  ```

  Note that it's necessary to specify `bash` explicitly because your
  default shell (e.g. `/bin/bash`) generally will not exist in the
  chroot.

# Description

`nix shell` runs a command in an environment in which the `$PATH` variable
provides the specified [*installables*](./nix.md#installables). If no command is specified, it starts the
default shell of your user account specified by `$SHELL`.

# Use as a `#!`-interpreter

You can use `nix` as a script interpreter to allow scripts written
in arbitrary languages to obtain their own dependencies via Nix. This is
done by starting the script with the following lines:

```bash
#! /usr/bin/env nix
#! nix shell installables --command real-interpreter
```

where *real-interpreter* is the “real” script interpreter that will be
invoked by `nix shell` after it has obtained the dependencies and
initialised the environment, and *installables* are the attribute names of
the dependencies in Nixpkgs.

The lines starting with `#! nix` specify options (see above). Note that you
cannot write `#! /usr/bin/env nix shell -i ...` because many operating systems
only allow one argument in `#!` lines.

For example, here is a Python script that depends on Python and the
`prettytable` package:

```python
#! /usr/bin/env nix
#! nix shell github:tomberek/-#python3With.prettytable --command python

import prettytable

# Print a simple table.
t = prettytable.PrettyTable(["N", "N^2"])
for n in range(1, 10): t.add_row([n, n * n])
print(t)
```

Similarly, the following is a Perl script that specifies that it
requires Perl and the `HTML::TokeParser::Simple` and `LWP` packages:

```perl
#! /usr/bin/env nix
#! nix shell github:tomberek/-#perlWith.HTMLTokeParserSimple.LWP --command perl -x

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
#! /usr/bin/env nix
#! nix shell --impure --expr ``
#! nix with (import (builtins.getFlake ''nixpkgs'') {});
#! nix terraform.withPlugins (plugins: [ plugins.openstack ])
#! nix ``
#! nix --command bash

terraform "$@"
```

> **Note**
>
> You must use double backticks (```` `` ````) when passing a simple Nix expression
> in a nix shell shebang.

Finally, using the merging of multiple nix shell shebangs the following
Haskell script uses a specific branch of Nixpkgs/NixOS (the 21.11 stable
branch):

```haskell
#!/usr/bin/env nix
#!nix shell --override-input nixpkgs github:NixOS/nixpkgs/nixos-21.11
#!nix github:tomberek/-#haskellWith.download-curl.tagsoup --command runghc

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

    #!nix shell --override-input nixpkgs github:NixOS/nixpkgs/eabc38219184cc3e04a974fe31857d8e0eac098d

You can also use a Nix expression to build your own dependencies. For example,
the Python example could have been written as:

```python
#! /usr/bin/env nix
#! nix shell --impure --file deps.nix -i python
```

where the file `deps.nix` in the same directory as the `#!`-script
contains:

```nix
with import <nixpkgs> {};
python3.withPackages (ps: with ps; [ prettytable ])
```


)""
