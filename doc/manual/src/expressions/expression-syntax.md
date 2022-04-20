# Expression Syntax

Here is a Nix expression for GNU Hello:

```nix
{ stdenv, fetchurl, perl }: ①

stdenv.mkDerivation { ②
  name = "hello-2.1.1"; ③
  builder = ./builder.sh; ④
  src = fetchurl { ⑤
    url = "ftp://ftp.nluug.nl/pub/gnu/hello/hello-2.1.1.tar.gz";
    sha256 = "1md7jsfd8pa45z73bz1kszpp01yw6x5ljkjk2hx7wl800any6465";
  };
  inherit perl; ⑥
}
```

This file is actually already in the Nix Packages collection in
`pkgs/applications/misc/hello/ex-1/default.nix`. It is customary to
place each package in a separate directory and call the single Nix
expression in that directory `default.nix`. The file has the following
elements (referenced from the figure by number):

1.  This states that the expression is a *function* that expects to be
    called with three arguments: `stdenv`, `fetchurl`, and `perl`. They
    are needed to build Hello, but we don't know how to build them here;
    that's why they are function arguments. `stdenv` is a package that
    is used by almost all Nix Packages; it provides a
    “standard” environment consisting of the things you would expect
    in a basic Unix environment: a C/C++ compiler (GCC, to be precise),
    the Bash shell, fundamental Unix tools such as `cp`, `grep`, `tar`,
    etc. `fetchurl` is a function that downloads files. `perl` is the
    Perl interpreter.
    
    Nix functions generally have the form `{ x, y, ..., z }: e` where
    `x`, `y`, etc. are the names of the expected arguments, and where
    *e* is the body of the function. So here, the entire remainder of
    the file is the body of the function; when given the required
    arguments, the body should describe how to build an instance of
    the Hello package.

2.  So we have to build a package. Building something from other stuff
    is called a *derivation* in Nix (as opposed to sources, which are
    built by humans instead of computers). We perform a derivation by
    calling `stdenv.mkDerivation`. `mkDerivation` is a function
    provided by `stdenv` that builds a package from a set of
    *attributes*. A set is just a list of key/value pairs where each
    key is a string and each value is an arbitrary Nix
    expression. They take the general form `{ name1 = expr1; ...
    nameN = exprN; }`.

3.  The attribute `name` specifies the symbolic name and version of
    the package. Nix doesn't really care about these things, but they
    are used by for instance `nix-env -q` to show a “human-readable”
    name for packages. This attribute is required by `mkDerivation`.

4.  The attribute `builder` specifies the builder. This attribute can
    sometimes be omitted, in which case `mkDerivation` will fill in a
    default builder (which does a `configure; make; make install`, in
    essence). Hello is sufficiently simple that the default builder
    would suffice, but in this case, we will show an actual builder
    for educational purposes. The value `./builder.sh` refers to the
    shell script shown in the [next section](build-script.md),
    discussed below.

5.  The builder has to know what the sources of the package are. Here,
    the attribute `src` is bound to the result of a call to the
    `fetchurl` function. Given a URL and a SHA-256 hash of the expected
    contents of the file at that URL, this function builds a derivation
    that downloads the file and checks its hash. So the sources are a
    dependency that like all other dependencies is built before Hello
    itself is built.
    
    Instead of `src` any other name could have been used, and in fact
    there can be any number of sources (bound to different attributes).
    However, `src` is customary, and it's also expected by the default
    builder (which we don't use in this example).

6.  Since the derivation requires Perl, we have to pass the value of the
    `perl` function argument to the builder. All attributes in the set
    are actually passed as environment variables to the builder, so
    declaring an attribute

    ```nix
    perl = perl;
    ```
    
    will do the trick: it binds an attribute `perl` to the function
    argument which also happens to be called `perl`. However, it looks a
    bit silly, so there is a shorter syntax. The `inherit` keyword
    causes the specified attributes to be bound to whatever variables
    with the same name happen to be in scope.
