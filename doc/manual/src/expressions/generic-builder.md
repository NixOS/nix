# Generic Builder Syntax

Recall that the [build script for GNU Hello](build-script.md) looked
something like this:

```bash
PATH=$perl/bin:$PATH
tar xvfz $src
cd hello-*
./configure --prefix=$out
make
make install
```

The builders for almost all Unix packages look like this — set up some
environment variables, unpack the sources, configure, build, and
install. For this reason the standard environment provides some Bash
functions that automate the build process. Here is what a builder using
the generic build facilities looks like:

```bash
buildInputs="$perl" ①

source $stdenv/setup ②

genericBuild ③
```

Here is what each line means:

1.  The `buildInputs` variable tells `setup` to use the indicated
    packages as “inputs”. This means that if a package provides a `bin`
    subdirectory, it's added to `PATH`; if it has a `include`
    subdirectory, it's added to GCC's header search path; and so on.
    (This is implemented in a modular way: `setup` tries to source the
    file `pkg/nix-support/setup-hook` of all dependencies. These “setup
    hooks” can then set up whatever environment variables they want; for
    instance, the setup hook for Perl sets the `PERL5LIB` environment
    variable to contain the `lib/site_perl` directories of all inputs.)

2.  The function `genericBuild` is defined in the file `$stdenv/setup`.

3.  The final step calls the shell function `genericBuild`, which
    performs the steps that were done explicitly in the previous build
    script. The generic builder is smart enough to figure out whether
    to unpack the sources using `gzip`, `bzip2`, etc.  It can be
    customised in many ways; see the Nixpkgs manual for details.

Discerning readers will note that the `buildInputs` could just as well
have been set in the Nix expression, like this:

```nix
  buildInputs = [ perl ];
```

The `perl` attribute can then be removed, and the builder becomes even
shorter:

```bash
source $stdenv/setup
genericBuild
```

In fact, `mkDerivation` provides a default builder that looks exactly
like that, so it is actually possible to omit the builder for Hello
entirely.
