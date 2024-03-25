R""(

# Examples

- Format the current flake: `$ nix fmt`

  This is an alias to `$ nix fmt .`

- Format specific folders and/or files: `$ nix fmt ./folder ./file.nix`

- Check formatting but not edit any files: `$ nix fmt -- --check /file.nix`

- Read code from standard input and print the formatted result to standard
  output: `$ nix fmt -- -- - <input.nix >formatted.nix`

A formatter must be configured in the flake output to work.

With [nixpkgs-fmt](https://github.com/nix-community/nixpkgs-fmt):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.nixpkgs-fmt;
  };
}
```

With [nixfmt](https://github.com/serokell/nixfmt):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.nixfmt;
  };
}
```

Note: the latest release of `nixfmt` doesn't support formatting folders (using
`nix fmt`), but it will be fixed it in the next release.

With [Alejandra](https://github.com/kamadorueda/alejandra):

```nix
# flake.nix
{
  outputs = { nixpkgs, self }: {
    formatter.x86_64-linux = nixpkgs.legacyPackages.x86_64-linux.alejandra;
  };
}
```

# Description

`nix fmt` will rewrite Nix files (\*.nix) to a canonical format
using the formatter specified in your flake.

If `--` occurs in arguments, all arguments after the first `--` are passed to
the formatter literally. You can pass extra arguments to the formatter instead
of Nix using `--`.

# Formatter Command Line Interface

The key words "MUST", "SHOULD" and "MAY" here are to be interpreted as
described in RFC 2119.

To make formatter invocations portable, we define Formatter Command Line
Interface. The main binary of the formatter package specified in the flake MUST
follow the command line interface below.

1.  It MUST accept one or more input paths of files. If a folder is given, it
    SHOULD add all \*.nix files recursively under it as inputs. It MAY support
    custom ignore rules besides the trivial recursion.

2.  It MUST support separator argument `--`. If `--` is given, all arguments
    after the first `--` MUST be treated as input paths as-is but not flags.

3.  If only one input path argument `-` is given and it is after `--`, the
    formatter MUST read the Nix file content to be formatted from the standard
    input, and write ONLY the formatted result content to the standard output.
    If the operation succeeds, no matter whether the result is different from
    the input, it MUST returns a zero exit code; otherwise, it MUST returns an
    non-zero exit code, and in this case, the content of the standard output is
    implementation defined.

4.  Except the case of reading from standard input, the formatter MUST format
    all input files in-place by default, and return a zero exit code for
    success. If any error occurs, it MUST return an non-zero exit code.

5.  If flag `-c` or `--check` is given, it MUST only check all inputs and not
    write back to any input paths. In the case of reading from standard input,
    it SHOULD print nothing to the standard output). It MUST returns a zero
    exit code when all inputs need no modification after formatting; otherwise,
    it MUST return an non-zero exit code.

6.  Except for flags defined above, extra flags MAY be supported, the meaning
    of which are implementation defined.

)""
