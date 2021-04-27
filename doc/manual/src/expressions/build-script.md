# Build Script

Here is the builder referenced from Hello's Nix expression (stored in
`pkgs/applications/misc/hello/ex-1/builder.sh`):

```bash
source $stdenv/setup ①

PATH=$perl/bin:$PATH ②

tar xvfz $src ③
cd hello-*
./configure --prefix=$out ④
make ⑤
make install
```

The builder can actually be made a lot shorter by using the *generic
builder* functions provided by `stdenv`, but here we write out the build
steps to elucidate what a builder does. It performs the following steps:

1.  When Nix runs a builder, it initially completely clears the
    environment (except for the attributes declared in the derivation).
    This is done to prevent undeclared inputs from being used in the
    build process. If for example the `PATH` contained `/usr/bin`, then
    you might accidentally use `/usr/bin/gcc`.
    
    So the first step is to set up the environment. This is done by
    calling the `setup` script of the standard environment. The
    environment variable `stdenv` points to the location of the
    standard environment being used. (It wasn't specified explicitly
    as an attribute in Hello's Nix expression, but `mkDerivation` adds
    it automatically.)

2.  Since Hello needs Perl, we have to make sure that Perl is in the
    `PATH`. The `perl` environment variable points to the location of
    the Perl package (since it was passed in as an attribute to the
    derivation), so `$perl/bin` is the directory containing the Perl
    interpreter.

3.  Now we have to unpack the sources. The `src` attribute was bound to
    the result of fetching the Hello source tarball from the network, so
    the `src` environment variable points to the location in the Nix
    store to which the tarball was downloaded. After unpacking, we `cd`
    to the resulting source directory.
    
    The whole build is performed in a temporary directory created in
    `/tmp`, by the way. This directory is removed after the builder
    finishes, so there is no need to clean up the sources afterwards.
    Also, the temporary directory is always newly created, so you don't
    have to worry about files from previous builds interfering with the
    current build.

4.  GNU Hello is a typical Autoconf-based package, so we first have to
    run its `configure` script. In Nix every package is stored in a
    separate location in the Nix store, for instance
    `/nix/store/9a54ba97fb71b65fda531012d0443ce2-hello-2.1.1`. Nix
    computes this path by cryptographically hashing all attributes of
    the derivation. The path is passed to the builder through the `out`
    environment variable. So here we give `configure` the parameter
    `--prefix=$out` to cause Hello to be installed in the expected
    location.

5.  Finally we build Hello (`make`) and install it into the location
    specified by `out` (`make install`).

If you are wondering about the absence of error checking on the result
of various commands called in the builder: this is because the shell
script is evaluated with Bash's `-e` option, which causes the script to
be aborted if any command fails without an error check.
